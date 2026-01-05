#include "fec_frame.hpp"
#include "logger.hpp"
#include <cstring>
#include <stdexcept>

namespace mpquic_fec {

// FECFrameHeader 序列化
std::vector<uint8_t> FECFrameHeader::serialize() const {
    std::vector<uint8_t> data(HEADER_SIZE);
    size_t offset = 0;
    
    // Frame Type (1 byte)
    data[offset++] = static_cast<uint8_t>(frame_type);
    
    // Group ID (8 bytes, big-endian)
    for (int i = 7; i >= 0; --i) {
        data[offset++] = static_cast<uint8_t>((group_id >> (i * 8)) & 0xFF);
    }
    
    // Block Index (4 bytes)
    for (int i = 3; i >= 0; --i) {
        data[offset++] = static_cast<uint8_t>((block_index >> (i * 8)) & 0xFF);
    }
    
    // Total Blocks (4 bytes)
    for (int i = 3; i >= 0; --i) {
        data[offset++] = static_cast<uint8_t>((total_blocks >> (i * 8)) & 0xFF);
    }
    
    // Payload Length (4 bytes)
    for (int i = 3; i >= 0; --i) {
        data[offset++] = static_cast<uint8_t>((payload_length >> (i * 8)) & 0xFF);
    }
    
    return data;
}

// FECFrameHeader 反序列化
FECFrameHeader FECFrameHeader::deserialize(const uint8_t* data, size_t len) {
    if (len < HEADER_SIZE) {
        throw std::invalid_argument("Insufficient data for FEC header");
    }
    
    FECFrameHeader header;
    size_t offset = 0;
    
    // Frame Type
    header.frame_type = static_cast<FrameType>(data[offset++]);
    
    // Group ID
    header.group_id = 0;
    for (int i = 0; i < 8; ++i) {
        header.group_id = (header.group_id << 8) | data[offset++];
    }
    
    // Block Index
    header.block_index = 0;
    for (int i = 0; i < 4; ++i) {
        header.block_index = (header.block_index << 8) | data[offset++];
    }
    
    // Total Blocks
    header.total_blocks = 0;
    for (int i = 0; i < 4; ++i) {
        header.total_blocks = (header.total_blocks << 8) | data[offset++];
    }
    
    // Payload Length
    header.payload_length = 0;
    for (int i = 0; i < 4; ++i) {
        header.payload_length = (header.payload_length << 8) | data[offset++];
    }
    
    return header;
}

// FECFrame 序列化
std::vector<uint8_t> FECFrame::serialize() const {
    auto header_data = header.serialize();
    std::vector<uint8_t> frame_data;
    frame_data.reserve(header_data.size() + payload.size());
    
    frame_data.insert(frame_data.end(), header_data.begin(), header_data.end());
    frame_data.insert(frame_data.end(), payload.begin(), payload.end());
    
    return frame_data;
}

// FECFrame 反序列化
FECFrame FECFrame::deserialize(const uint8_t* data, size_t len) {
    FECFrame frame;
    
    frame.header = FECFrameHeader::deserialize(data, len);
    
    if (len < FECFrameHeader::HEADER_SIZE + frame.header.payload_length) {
        throw std::invalid_argument("Insufficient data for FEC frame payload");
    }
    
    const uint8_t* payload_start = data + FECFrameHeader::HEADER_SIZE;
    frame.payload.assign(payload_start, payload_start + frame.header.payload_length);
    
    return frame;
}

// PacketNumberMapper 实现

void PacketNumberMapper::add_mapping(uint64_t group_id, uint32_t block_idx,
                                     uint32_t path_id, uint64_t pkt_num, bool is_repair) {
    PacketMapping mapping;
    mapping.group_id = group_id;
    mapping.block_index = block_idx;
    mapping.path_id = path_id;
    mapping.packet_number = pkt_num;
    mapping.is_repair = is_repair;
    
    auto key = std::make_pair(path_id, pkt_num);
    pkt_to_mapping_[key] = mapping;
    group_to_mappings_[group_id].push_back(mapping);
    
    LOG_DEBUG("Added mapping: Group ", group_id, ", Block ", block_idx,
              ", Path ", path_id, ", Pkt ", pkt_num, ", Repair=", is_repair);
}

PacketNumberMapper::PacketMapping* PacketNumberMapper::find_by_packet(
    uint32_t path_id, uint64_t packet_number) {
    auto key = std::make_pair(path_id, packet_number);
    auto it = pkt_to_mapping_.find(key);
    if (it != pkt_to_mapping_.end()) {
        return &(it->second);
    }
    return nullptr;
}

std::vector<PacketNumberMapper::PacketMapping> 
PacketNumberMapper::find_by_group(uint64_t group_id) {
    auto it = group_to_mappings_.find(group_id);
    if (it != group_to_mappings_.end()) {
        return it->second;
    }
    return {};
}

void PacketNumberMapper::cleanup_old_mappings(uint64_t before_group_id) {
    // 清理指定group_id之前的所有映射
    std::vector<uint64_t> groups_to_remove;
    
    for (auto& [gid, mappings] : group_to_mappings_) {
        if (gid < before_group_id) {
            groups_to_remove.push_back(gid);
            
            // 从pkt_to_mapping_中移除
            for (const auto& mapping : mappings) {
                auto key = std::make_pair(mapping.path_id, mapping.packet_number);
                pkt_to_mapping_.erase(key);
            }
        }
    }
    
    for (auto gid : groups_to_remove) {
        group_to_mappings_.erase(gid);
    }
    
    LOG_DEBUG("Cleaned up ", groups_to_remove.size(), " old FEC groups");
}

} // namespace mpquic_fec
