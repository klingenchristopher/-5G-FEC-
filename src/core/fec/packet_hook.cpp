#include "packet_hook.hpp"
#include "logger.hpp"
#include <chrono>
#include <algorithm>

namespace mpquic_fec {

// ========== FECGroupManager 实现 ==========

FECGroupManager::FECGroupManager(uint32_t default_k, uint32_t default_m, 
                                 uint32_t block_size)
    : current_k_(default_k), current_m_(default_m), block_size_(block_size),
      next_group_id_(1) {
    
    encoder_ = std::make_unique<FECEncoder>(current_k_, current_m_, block_size_);
    current_group_ = create_new_group();
    
    LOG_INFO("FECGroupManager initialized: k=", current_k_, ", m=", current_m_,
             ", block_size=", block_size_);
}

uint64_t FECGroupManager::add_source_packet(const PendingPacket& packet) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    // 添加到当前组
    current_group_->source_packets.push_back(packet);
    
    LOG_DEBUG("Added packet to group ", current_group_->group_id, 
              " (", current_group_->source_packets.size(), "/", current_k_, ")");
    
    // 检查是否形成完整编码组
    if (current_group_->source_packets.size() >= current_k_) {
        uint64_t completed_group_id = current_group_->group_id;
        
        // 执行编码
        perform_encoding(current_group_);
        
        // 保存到已完成列表
        encoded_groups_[completed_group_id] = current_group_;
        
        // 创建新的编码组
        current_group_ = create_new_group();
        
        LOG_INFO("Completed FEC encoding for group ", completed_group_id);
        return completed_group_id;
    }
    
    return 0;  // 组尚未完成
}

std::shared_ptr<EncodingGroup> FECGroupManager::get_encoded_group(uint64_t group_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = encoded_groups_.find(group_id);
    if (it != encoded_groups_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<uint64_t> FECGroupManager::flush_pending_groups() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<uint64_t> flushed_ids;
    
    // 如果当前组有数据但不满k个，强制编码
    if (!current_group_->source_packets.empty()) {
        // 填充空包到k个
        while (current_group_->source_packets.size() < current_k_) {
            PendingPacket padding;
            padding.packet_number = 0;
            padding.path_id = 0;
            padding.data.resize(block_size_, 0);
            padding.timestamp_us = get_timestamp_us();
            current_group_->source_packets.push_back(padding);
        }
        
        uint64_t group_id = current_group_->group_id;
        perform_encoding(current_group_);
        encoded_groups_[group_id] = current_group_;
        flushed_ids.push_back(group_id);
        
        current_group_ = create_new_group();
        
        LOG_INFO("Flushed incomplete group ", group_id);
    }
    
    return flushed_ids;
}

void FECGroupManager::update_coding_params(uint32_t k, uint32_t m) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (k != current_k_ || m != current_m_) {
        LOG_INFO("Updating FEC params: k=", k, ", m=", m, 
                 " (was k=", current_k_, ", m=", current_m_, ")");
        
        current_k_ = k;
        current_m_ = m;
        
        // 重新创建编码器
        encoder_ = std::make_unique<FECEncoder>(current_k_, current_m_, block_size_);
        
        // 刷新当前未完成的组
        flush_pending_groups();
    }
}

void FECGroupManager::cleanup_old_groups(uint64_t before_group_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    auto it = encoded_groups_.begin();
    while (it != encoded_groups_.end()) {
        if (it->first < before_group_id) {
            it = encoded_groups_.erase(it);
        } else {
            ++it;
        }
    }
    
    LOG_DEBUG("Cleaned up old FEC groups before ", before_group_id);
}

void FECGroupManager::perform_encoding(std::shared_ptr<EncodingGroup> group) {
    // 准备编码数据
    std::vector<std::vector<uint8_t>> data_blocks;
    for (const auto& packet : group->source_packets) {
        data_blocks.push_back(packet.data);
    }
    
    // 执行FEC编码
    auto parity_blocks = encoder_->encode(data_blocks);
    
    // 创建修复帧
    group->repair_frames.clear();
    for (size_t i = 0; i < parity_blocks.size(); ++i) {
        FECFrame repair_frame;
        repair_frame.header.frame_type = FrameType::FEC_REPAIR_FRAME;
        repair_frame.header.group_id = group->group_id;
        repair_frame.header.block_index = current_k_ + i;
        repair_frame.header.total_blocks = current_k_ + current_m_;
        repair_frame.header.payload_length = parity_blocks[i].size();
        repair_frame.payload = parity_blocks[i];
        
        group->repair_frames.push_back(repair_frame);
    }
    
    group->is_encoded = true;
    
    LOG_DEBUG("Encoded group ", group->group_id, ": ", current_k_, " source + ",
              current_m_, " repair blocks");
}

std::shared_ptr<EncodingGroup> FECGroupManager::create_new_group() {
    auto group = std::make_shared<EncodingGroup>();
    group->group_id = next_group_id_++;
    group->info.group_id = group->group_id;
    group->info.k = current_k_;
    group->info.m = current_m_;
    group->info.block_size = block_size_;
    group->info.timestamp_us = get_timestamp_us();
    group->is_encoded = false;
    group->created_time_us = group->info.timestamp_us;
    
    return group;
}

uint64_t FECGroupManager::get_timestamp_us() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

// ========== PacketSendHook 实现 ==========

PacketSendHook::PacketSendHook(std::shared_ptr<FECGroupManager> group_mgr)
    : group_manager_(group_mgr), fec_enabled_(true) {
    
    LOG_INFO("PacketSendHook initialized");
}

bool PacketSendHook::on_packet_send(uint64_t packet_num, uint32_t path_id,
                                    const std::vector<uint8_t>& stream_data,
                                    std::vector<FECFrame>& out_packets) {
    if (!fec_enabled_) {
        // FEC未启用，直接返回
        return false;
    }
    
    // 1. 创建待编码包
    PendingPacket pending;
    pending.packet_number = packet_num;
    pending.path_id = path_id;
    pending.data = stream_data;
    pending.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // 2. 添加到编码组管理器
    uint64_t completed_group_id = group_manager_->add_source_packet(pending);
    
    // 3. 如果形成了完整编码组，获取编码结果
    if (completed_group_id > 0) {
        auto group = group_manager_->get_encoded_group(completed_group_id);
        if (group && group->is_encoded) {
            // 生成源帧
            for (size_t i = 0; i < group->source_packets.size(); ++i) {
                FECFrame source_frame = wrap_source_frame(
                    group->group_id, i, 
                    group->info.k + group->info.m,
                    group->source_packets[i].data);
                out_packets.push_back(source_frame);
            }
            
            // 添加修复帧
            for (const auto& repair_frame : group->repair_frames) {
                out_packets.push_back(repair_frame);
            }
            
            LOG_INFO("Generated ", out_packets.size(), " FEC frames for group ",
                     completed_group_id);
            return true;
        }
    }
    
    return false;
}

bool PacketSendHook::has_pending_frames() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !pending_frames_.empty();
}

std::vector<FECFrame> PacketSendHook::pop_pending_frames() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::vector<FECFrame> frames;
    
    while (!pending_frames_.empty()) {
        frames.push_back(pending_frames_.front());
        pending_frames_.pop();
    }
    
    return frames;
}

FECFrame PacketSendHook::wrap_source_frame(uint64_t group_id, uint32_t block_idx,
                                          uint32_t total_blocks,
                                          const std::vector<uint8_t>& data) {
    FECFrame frame;
    frame.header.frame_type = FrameType::FEC_SOURCE_FRAME;
    frame.header.group_id = group_id;
    frame.header.block_index = block_idx;
    frame.header.total_blocks = total_blocks;
    frame.header.payload_length = data.size();
    frame.payload = data;
    
    return frame;
}

// ========== PacketReceiveHook 实现 ==========

PacketReceiveHook::PacketReceiveHook() {
    LOG_INFO("PacketReceiveHook initialized");
}

std::vector<std::vector<uint8_t>> PacketReceiveHook::on_frame_received(const FECFrame& frame) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    uint64_t group_id = frame.header.group_id;
    
    // 获取或创建接收组
    auto& recv_group = received_groups_[group_id];
    
    // 存储接收到的帧
    recv_group.received_frames[frame.header.block_index] = frame;
    
    // 更新组信息
    if (recv_group.info.group_id == 0) {
        recv_group.info.group_id = group_id;
        // 从帧头推断k和m（简化处理）
        uint32_t total = frame.header.total_blocks;
        recv_group.info.k = (total * 2) / 3;  // 假设k:m = 2:1
        recv_group.info.m = total - recv_group.info.k;
        recv_group.info.block_size = frame.payload.size();
    }
    
    LOG_DEBUG("Received FEC frame: Group ", group_id, ", Block ", 
              frame.header.block_index, " (", recv_group.received_frames.size(), "/",
              recv_group.info.k, ")");
    
    // 检查是否可以解码
    if (recv_group.received_frames.size() >= recv_group.info.k) {
        return try_decode_group(group_id);
    }
    
    return {};
}

bool PacketReceiveHook::can_decode_group(uint64_t group_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = received_groups_.find(group_id);
    if (it != received_groups_.end()) {
        return it->second.received_frames.size() >= it->second.info.k;
    }
    return false;
}

std::vector<std::vector<uint8_t>> PacketReceiveHook::try_decode_group(uint64_t group_id) {
    auto& recv_group = received_groups_[group_id];
    
    if (recv_group.is_complete) {
        return {};  // 已解码过
    }
    
    // 获取或创建解码器
    auto key = std::make_pair(recv_group.info.k, recv_group.info.m);
    if (decoders_.find(key) == decoders_.end()) {
        decoders_[key] = std::make_unique<FECDecoder>(
            recv_group.info.k, recv_group.info.m, recv_group.info.block_size);
    }
    
    // 准备解码数据
    std::vector<std::vector<uint8_t>> received_blocks;
    std::vector<uint32_t> block_ids;
    
    for (const auto& [block_idx, frame] : recv_group.received_frames) {
        received_blocks.push_back(frame.payload);
        block_ids.push_back(block_idx);
    }
    
    // 执行解码
    try {
        auto decoded = decoders_[key]->decode(received_blocks, block_ids);
        recv_group.is_complete = true;
        
        LOG_INFO("Successfully decoded group ", group_id, ", recovered ",
                 decoded.size(), " blocks");
        
        return decoded;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to decode group ", group_id, ": ", e.what());
        return {};
    }
}

} // namespace mpquic_fec
