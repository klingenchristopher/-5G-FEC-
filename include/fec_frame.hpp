#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <map>

namespace mpquic_fec {

/**
 * @brief FEC帧类型定义
 * 
 * 在MP-QUIC中定义自定义的FEC_FRAME类型，用于封装FEC编码后的数据
 */
enum class FrameType : uint8_t {
    STREAM_FRAME = 0x08,      // 标准QUIC流帧
    FEC_SOURCE_FRAME = 0xF0,  // FEC源数据帧
    FEC_REPAIR_FRAME = 0xF1   // FEC修复帧（冗余帧）
};

/**
 * @brief FEC编码组信息
 * 
 * 用于标识一组经过FEC编码的数据包
 */
struct FECGroupInfo {
    uint64_t group_id;        // 编码组ID（全局唯一）
    uint32_t k;               // 源数据包数量
    uint32_t m;               // 冗余包数量
    uint32_t block_size;      // 每个块的大小
    uint64_t timestamp_us;    // 编码时间戳（微秒）
    
    FECGroupInfo() 
        : group_id(0), k(0), m(0), block_size(0), timestamp_us(0) {}
    
    FECGroupInfo(uint64_t gid, uint32_t k_val, uint32_t m_val, uint32_t bs, uint64_t ts)
        : group_id(gid), k(k_val), m(m_val), block_size(bs), timestamp_us(ts) {}
};

/**
 * @brief FEC帧头部结构
 * 
 * 包含FEC帧的元数据信息
 */
struct FECFrameHeader {
    FrameType frame_type;      // 帧类型（SOURCE或REPAIR）
    uint64_t group_id;         // 编码组ID
    uint32_t block_index;      // 块在组内的索引
    uint32_t total_blocks;     // 组内总块数（k+m）
    uint32_t payload_length;   // payload长度
    
    // 序列化到字节流
    std::vector<uint8_t> serialize() const;
    
    // 从字节流反序列化
    static FECFrameHeader deserialize(const uint8_t* data, size_t len);
    
    // 头部固定大小
    static constexpr size_t HEADER_SIZE = 25;
};

/**
 * @brief FEC帧完整结构
 */
struct FECFrame {
    FECFrameHeader header;
    std::vector<uint8_t> payload;
    
    // 获取完整帧大小
    size_t total_size() const {
        return FECFrameHeader::HEADER_SIZE + payload.size();
    }
    
    // 序列化整个帧
    std::vector<uint8_t> serialize() const;
    
    // 反序列化
    static FECFrame deserialize(const uint8_t* data, size_t len);
    
    // 是否为源数据帧
    bool is_source_frame() const {
        return header.frame_type == FrameType::FEC_SOURCE_FRAME;
    }
    
    // 是否为修复帧
    bool is_repair_frame() const {
        return header.frame_type == FrameType::FEC_REPAIR_FRAME;
    }
};

/**
 * @brief 包号空间映射表
 * 
 * 记录FEC Group ID与各路径Packet Number的映射关系
 * 解决QUIC多路径独立包号空间的问题
 */
class PacketNumberMapper {
public:
    struct PacketMapping {
        uint64_t group_id;
        uint32_t block_index;
        uint32_t path_id;
        uint64_t packet_number;
        bool is_repair;
        
        PacketMapping() : group_id(0), block_index(0), path_id(0), 
                         packet_number(0), is_repair(false) {}
    };
    
    // 添加映射
    void add_mapping(uint64_t group_id, uint32_t block_idx, 
                    uint32_t path_id, uint64_t pkt_num, bool is_repair);
    
    // 根据packet number查找映射
    PacketMapping* find_by_packet(uint32_t path_id, uint64_t packet_number);
    
    // 根据group id查找该组的所有包
    std::vector<PacketMapping> find_by_group(uint64_t group_id);
    
    // 清理过期映射（避免内存泄漏）
    void cleanup_old_mappings(uint64_t before_group_id);
    
private:
    // 使用组合键存储映射
    std::map<std::pair<uint32_t, uint64_t>, PacketMapping> pkt_to_mapping_;
    std::map<uint64_t, std::vector<PacketMapping>> group_to_mappings_;
};

} // namespace mpquic_fec
