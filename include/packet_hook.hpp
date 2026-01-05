#pragma once

#include "fec_encoder.hpp"
#include "fec_frame.hpp"
#include "buffer_manager.hpp"
#include <queue>
#include <memory>
#include <mutex>
#include <map>

namespace mpquic_fec {

/**
 * @brief 待编码的数据包信息
 */
struct PendingPacket {
    uint64_t packet_number;      // 包序号
    uint32_t path_id;            // 原始路径ID
    std::vector<uint8_t> data;   // 原始数据
    uint64_t timestamp_us;       // 时间戳
    
    PendingPacket() : packet_number(0), path_id(0), timestamp_us(0) {}
};

/**
 * @brief 编码组状态
 */
struct EncodingGroup {
    uint64_t group_id;
    FECGroupInfo info;
    std::vector<PendingPacket> source_packets;
    std::vector<FECFrame> repair_frames;
    bool is_encoded;
    uint64_t created_time_us;
    
    EncodingGroup() : group_id(0), is_encoded(false), created_time_us(0) {}
};

/**
 * @brief FEC编码组管理器
 * 
 * 核心职责：
 * 1. 收集待编码的数据包，按组进行管理
 * 2. 触发ISA-L编码生成冗余包
 * 3. 管理编码组的生命周期
 */
class FECGroupManager {
public:
    FECGroupManager(uint32_t default_k = 4, uint32_t default_m = 2, 
                   uint32_t block_size = 1200);
    ~FECGroupManager() = default;
    
    /**
     * @brief 添加待编码的数据包
     * @return 如果形成完整编码组，返回组ID；否则返回0
     */
    uint64_t add_source_packet(const PendingPacket& packet);
    
    /**
     * @brief 获取已编码完成的组
     */
    std::shared_ptr<EncodingGroup> get_encoded_group(uint64_t group_id);
    
    /**
     * @brief 强制编码当前未完成的组（用于超时或主动刷新）
     */
    std::vector<uint64_t> flush_pending_groups();
    
    /**
     * @brief 更新编码参数 (k, m)
     */
    void update_coding_params(uint32_t k, uint32_t m);
    
    /**
     * @brief 清理已完成的编码组
     */
    void cleanup_old_groups(uint64_t before_group_id);
    
    /**
     * @brief 获取当前编码参数
     */
    std::pair<uint32_t, uint32_t> get_coding_params() const {
        return {current_k_, current_m_};
    }
    
private:
    // 当前编码参数
    uint32_t current_k_;
    uint32_t current_m_;
    uint32_t block_size_;
    
    // 编码器
    std::unique_ptr<FECEncoder> encoder_;
    
    // 当前正在积累的编码组
    std::shared_ptr<EncodingGroup> current_group_;
    
    // 已完成的编码组缓存
    std::map<uint64_t, std::shared_ptr<EncodingGroup>> encoded_groups_;
    
    // 下一个组ID
    uint64_t next_group_id_;
    
    // 线程安全
    std::mutex mutex_;
    
    // 执行FEC编码
    void perform_encoding(std::shared_ptr<EncodingGroup> group);
    
    // 创建新的编码组
    std::shared_ptr<EncodingGroup> create_new_group();
    
    // 获取当前时间戳（微秒）
    uint64_t get_timestamp_us() const;
};

/**
 * @brief Packet Hook点 - 拦截发送管道
 * 
 * 这是FEC逻辑注入MP-QUIC的核心Hook点
 * 位置：在Stream Frame生成之后，Packet调度之前
 */
class PacketSendHook {
public:
    PacketSendHook(std::shared_ptr<FECGroupManager> group_mgr);
    ~PacketSendHook() = default;
    
    /**
     * @brief Hook入口：拦截即将发送的数据包
     * 
     * @param packet_num 包序号
     * @param path_id 原始目标路径
     * @param stream_data 流数据
     * @param out_packets 输出：需要发送的包列表（可能包含源包和冗余包）
     * @return 是否成功处理
     */
    bool on_packet_send(uint64_t packet_num, uint32_t path_id,
                       const std::vector<uint8_t>& stream_data,
                       std::vector<FECFrame>& out_packets);
    
    /**
     * @brief 设置是否启用FEC
     */
    void set_fec_enabled(bool enabled) { fec_enabled_ = enabled; }
    
    /**
     * @brief 获取待发送的FEC帧队列
     */
    bool has_pending_frames() const;
    std::vector<FECFrame> pop_pending_frames();
    
private:
    std::shared_ptr<FECGroupManager> group_manager_;
    bool fec_enabled_;
    
    // 待发送的FEC帧队列
    std::queue<FECFrame> pending_frames_;
    mutable std::mutex queue_mutex_;
    
    // 包装原始数据为FEC源帧
    FECFrame wrap_source_frame(uint64_t group_id, uint32_t block_idx,
                               uint32_t total_blocks, 
                               const std::vector<uint8_t>& data);
};

/**
 * @brief 接收端解码器
 * 
 * 对应发送端的Hook，负责接收并解码FEC帧
 */
class PacketReceiveHook {
public:
    PacketReceiveHook();
    ~PacketReceiveHook() = default;
    
    /**
     * @brief 接收FEC帧
     * @return 如果成功恢复了数据，返回恢复的原始数据
     */
    std::vector<std::vector<uint8_t>> on_frame_received(const FECFrame& frame);
    
    /**
     * @brief 检查是否有完整的编码组可以解码
     */
    bool can_decode_group(uint64_t group_id);
    
private:
    // 接收缓冲区：按组ID组织
    struct ReceivedGroup {
        FECGroupInfo info;
        std::map<uint32_t, FECFrame> received_frames;  // block_index -> frame
        bool is_complete;
        
        ReceivedGroup() : is_complete(false) {}
    };
    
    std::map<uint64_t, ReceivedGroup> received_groups_;
    std::mutex mutex_;
    
    // 解码器映射（按k,m缓存）
    std::map<std::pair<uint32_t, uint32_t>, std::unique_ptr<FECDecoder>> decoders_;
    
    // 尝试解码组
    std::vector<std::vector<uint8_t>> try_decode_group(uint64_t group_id);
};

} // namespace mpquic_fec
