#include "mpquic_fec_controller.hpp"
#include "logger.hpp"
#include <chrono>
#include <algorithm>

namespace mpquic_fec {

MPQUICFECController::MPQUICFECController(uint32_t default_k, uint32_t default_m,
                                         uint32_t block_size)
    : fec_enabled_(true), block_size_(block_size), last_update_time_us_(0) {
    
    // 创建核心组件
    group_manager_ = std::make_shared<FECGroupManager>(default_k, default_m, block_size);
    send_hook_ = std::make_shared<PacketSendHook>(group_manager_);
    receive_hook_ = std::make_shared<PacketReceiveHook>();
    path_scheduler_ = std::make_shared<PathScheduler>();
    oco_controller_ = std::make_shared<OCORedundancyController>();
    pkt_mapper_ = std::make_shared<PacketNumberMapper>();
    fec_strategy_ = std::make_shared<AdaptiveFECStrategy>();
    
    // 连接组件
    path_scheduler_->set_oco_controller(oco_controller_);
    
    LOG_INFO("MPQUICFECController initialized with k=", default_k, ", m=", default_m);
}

void MPQUICFECController::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 初始化决策
    current_decision_.k = 4;
    current_decision_.m = 2;
    current_decision_.redundancy_rate = 0.5;
    
    last_update_time_us_ = get_timestamp_us();
    
    LOG_INFO("MPQUICFECController initialized successfully");
}

void MPQUICFECController::add_path(uint32_t path_id, const PathState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    path_scheduler_->update_path_state(state);
    
    // 初始化包序号
    next_packet_numbers_[path_id] = 1;
    
    // 更新OCO控制器
    LinkMetrics metrics;
    metrics.path_id = path_id;
    metrics.rtt_ms = state.rtt_ms;
    metrics.loss_rate = state.loss_rate;
    metrics.bandwidth_mbps = state.bandwidth_mbps;
    metrics.jitter_ms = state.jitter_ms;
    oco_controller_->update_link_metrics(metrics);
    
    LOG_INFO("Added path ", path_id, " to FEC controller");
}

void MPQUICFECController::update_path_state(const PathState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    path_scheduler_->update_path_state(state);
    
    // 同步到OCO控制器
    LinkMetrics metrics;
    metrics.path_id = state.path_id;
    metrics.rtt_ms = state.rtt_ms;
    metrics.loss_rate = state.loss_rate;
    metrics.bandwidth_mbps = state.bandwidth_mbps;
    metrics.jitter_ms = state.jitter_ms;
    metrics.packets_sent = state.bytes_sent / 1200;  // 估算包数
    metrics.bytes_in_flight = state.cwnd;
    
    oco_controller_->update_link_metrics(metrics);
}

void MPQUICFECController::update_loss_correlation(uint32_t path_i, uint32_t path_j, double rho) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    path_scheduler_->update_path_correlation(path_i, path_j, rho);
    oco_controller_->update_loss_correlation(path_i, path_j, rho);
    
    LOG_DEBUG("Updated loss correlation: ", path_i, " <-> ", path_j, " = ", rho);
}

std::vector<SendPacketMeta> MPQUICFECController::send_stream_data(
    const std::vector<uint8_t>& stream_data, uint32_t original_path_id) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<SendPacketMeta> result;
    
    if (!fec_enabled_) {
        // FEC未启用，直接发送
        SendPacketMeta meta;
        meta.packet_number = get_next_packet_number(original_path_id);
        meta.path_id = original_path_id;
        // 创建简单的源帧
        meta.frame.header.frame_type = FrameType::FEC_SOURCE_FRAME;
        meta.frame.payload = stream_data;
        meta.send_time_us = get_timestamp_us();
        meta.is_repair = false;
        
        result.push_back(meta);
        return result;
    }
    
    // 步骤1：Hook拦截 - 将数据提交给FEC编码组管理器
    std::vector<FECFrame> fec_frames;
    uint64_t fake_pkt_num = get_next_packet_number(original_path_id) - 1;
    
    bool has_encoded = send_hook_->on_packet_send(
        fake_pkt_num, original_path_id, stream_data, fec_frames);
    
    // 步骤2：如果完成了编码组，进行路径分配
    if (has_encoded && !fec_frames.empty()) {
        assign_packets_to_paths(fec_frames, result);
        stats_.fec_groups_created++;
        
        LOG_INFO("Encoded and assigned ", result.size(), " packets (",
                 stats_.source_packets_sent, " source + ", 
                 stats_.repair_packets_sent, " repair)");
    }
    
    return result;
}

std::vector<std::vector<uint8_t>> MPQUICFECController::receive_fec_frame(
    const FECFrame& frame, uint32_t from_path_id [[maybe_unused]]) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 调用接收Hook进行解码
    auto recovered = receive_hook_->on_frame_received(frame);
    
    if (!recovered.empty()) {
        stats_.packets_recovered += recovered.size();
        LOG_INFO("Recovered ", recovered.size(), " packets from FEC decoding");
    }
    
    return recovered;
}

void MPQUICFECController::on_ack_received(uint32_t path_id, uint64_t packet_number, 
                                         uint64_t rtt_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 查找包映射
    auto mapping = pkt_mapper_->find_by_packet(path_id, packet_number);
    
    if (mapping) {
        LOG_DEBUG("ACK received: Path ", path_id, ", Pkt ", packet_number,
                  ", Group ", mapping->group_id, ", RTT ", rtt_us / 1000.0, "ms");
    }
    
    // 反馈到OCO控制器
    // oco_controller_->feedback_update(actual_loss, rtt_us / 1000.0);
}

void MPQUICFECController::on_packet_lost(uint32_t path_id, uint64_t packet_number) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto mapping = pkt_mapper_->find_by_packet(path_id, packet_number);
    
    if (mapping) {
        LOG_INFO("Packet lost: Path ", path_id, ", Pkt ", packet_number,
                 ", Group ", mapping->group_id, 
                 ", Type ", (mapping->is_repair ? "REPAIR" : "SOURCE"));
        
        // 如果是源包丢失，可能需要从修复包恢复
        if (!mapping->is_repair) {
            // 触发FEC解码尝试
        }
    }
}

void MPQUICFECController::periodic_update() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t now = get_timestamp_us();
    uint64_t elapsed_ms = (now - last_update_time_us_) / 1000;
    
    if (elapsed_ms < 100) {
        return;  // 至少间隔100ms
    }
    
    // 步骤1：OCO决策更新
    update_fec_parameters();
    
    // 步骤2：刷新未完成的编码组
    auto flushed = group_manager_->flush_pending_groups();
    
    // 步骤3：清理过期映射
    if (stats_.fec_groups_created > 1000) {
        uint64_t cleanup_before = stats_.fec_groups_created - 500;
        pkt_mapper_->cleanup_old_mappings(cleanup_before);
        group_manager_->cleanup_old_groups(cleanup_before);
    }
    
    last_update_time_us_ = now;
    
    LOG_DEBUG("Periodic update completed, flushed ", flushed.size(), " groups");
}

void MPQUICFECController::set_fec_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    fec_enabled_ = enabled;
    send_hook_->set_fec_enabled(enabled);
    
    LOG_INFO("FEC ", (enabled ? "enabled" : "disabled"));
}

void MPQUICFECController::set_fec_strategy(AdaptiveFECStrategy::Strategy strategy) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 根据策略调整冗余率约束
    auto [min_rate, max_rate] = fec_strategy_->get_strategy_redundancy_range(strategy);
    oco_controller_->set_redundancy_constraints(min_rate, max_rate);
    
    LOG_INFO("FEC strategy set, redundancy rate: [", min_rate * 100, "%, ",
             max_rate * 100, "%]");
}

void MPQUICFECController::update_fec_parameters() {
    // 调用OCO控制器计算最优冗余度
    current_decision_ = oco_controller_->compute_optimal_redundancy();
    
    // 更新编码组管理器的参数
    auto [current_k, current_m] = group_manager_->get_coding_params();
    
    if (current_k != current_decision_.k || current_m != current_decision_.m) {
        group_manager_->update_coding_params(current_decision_.k, current_decision_.m);
        stats_.current_redundancy_rate = current_decision_.redundancy_rate;
        
        LOG_INFO("Updated FEC parameters: k=", current_decision_.k, 
                 ", m=", current_decision_.m,
                 " (redundancy=", current_decision_.redundancy_rate * 100, "%)");
    }
}

void MPQUICFECController::assign_packets_to_paths(const std::vector<FECFrame>& frames,
                                                  std::vector<SendPacketMeta>& out_packets) {
    // 获取路径选择
    uint32_t source_path = path_scheduler_->select_source_path(block_size_);
    uint32_t repair_path = path_scheduler_->select_repair_path(source_path, block_size_);
    
    for (const auto& frame : frames) {
        SendPacketMeta meta;
        meta.frame = frame;
        meta.send_time_us = get_timestamp_us();
        
        // 根据帧类型选择路径
        if (frame.is_source_frame()) {
            meta.path_id = source_path;
            meta.is_repair = false;
            meta.packet_number = get_next_packet_number(source_path);
            stats_.source_packets_sent++;
        } else {
            meta.path_id = repair_path;
            meta.is_repair = true;
            meta.packet_number = get_next_packet_number(repair_path);
            stats_.repair_packets_sent++;
        }
        
        stats_.total_packets_sent++;
        
        // 记录包号映射
        pkt_mapper_->add_mapping(
            frame.header.group_id,
            frame.header.block_index,
            meta.path_id,
            meta.packet_number,
            meta.is_repair
        );
        
        out_packets.push_back(meta);
    }
    
    LOG_DEBUG("Assigned ", frames.size(), " packets: ",
              "Source -> Path ", source_path, ", Repair -> Path ", repair_path);
}

uint64_t MPQUICFECController::get_next_packet_number(uint32_t path_id) {
    return next_packet_numbers_[path_id]++;
}

uint64_t MPQUICFECController::get_timestamp_us() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

void MPQUICFECController::update_statistics(const std::vector<SendPacketMeta>& packets) {
    for (const auto& pkt : packets) {
        stats_.total_packets_sent++;
        if (pkt.is_repair) {
            stats_.repair_packets_sent++;
        } else {
            stats_.source_packets_sent++;
        }
    }
}

} // namespace mpquic_fec
