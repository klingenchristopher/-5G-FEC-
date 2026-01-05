#pragma once

#include "packet_hook.hpp"
#include "path_scheduler.hpp"
#include "oco_controller.hpp"
#include "fec_frame.hpp"
#include <memory>
#include <queue>
#include <mutex>

namespace mpquic_fec {

/**
 * @brief 发送数据包的元数据
 */
struct SendPacketMeta {
    uint64_t packet_number;
    uint32_t path_id;
    FECFrame frame;
    uint64_t send_time_us;
    bool is_repair;
    
    SendPacketMeta() : packet_number(0), path_id(0), send_time_us(0), is_repair(false) {}
};

/**
 * @brief MP-QUIC FEC 数据流控制器
 * 
 * 这是整个系统的核心协调者，负责：
 * 1. 拦截Stream Frame -> 触发FEC编码
 * 2. 调用OCO决策器 -> 动态调整冗余参数
 * 3. 调用路径调度器 -> 分配Source/Repair包到不同路径
 * 4. 管理Packet Number映射 -> 处理多路径包号空间
 * 5. 提供统一的发送/接收接口
 * 
 * 这个类实现了文档中描述的完整Hook流程：
 * [Stream Data] -> [Hook: Packet Builder] -> [ISA-L Encoder] 
 *                                          -> [Hook: OCO Scheduler] 
 *                                          -> [Path 1/2/...]
 */
class MPQUICFECController {
public:
    MPQUICFECController(uint32_t default_k = 4, uint32_t default_m = 2,
                       uint32_t block_size = 1200);
    ~MPQUICFECController() = default;
    
    /**
     * @brief 初始化系统
     */
    void initialize();
    
    /**
     * @brief 添加新路径
     */
    void add_path(uint32_t path_id, const PathState& state);
    
    /**
     * @brief 更新路径状态（从ACK/链路监测获取）
     */
    void update_path_state(const PathState& state);
    
    /**
     * @brief 更新路径间丢包相关性
     */
    void update_loss_correlation(uint32_t path_i, uint32_t path_j, double rho);
    
    /**
     * @brief 发送数据包（核心Hook入口）
     * 
     * 这是MP-QUIC发送管道的Hook点
     * 
     * @param stream_data 流数据
     * @param original_path_id 原始目标路径（可能会被重新分配）
     * @return 实际发送的数据包列表（包含源包和冗余包）
     */
    std::vector<SendPacketMeta> send_stream_data(const std::vector<uint8_t>& stream_data,
                                                 uint32_t original_path_id = 0);
    
    /**
     * @brief 接收数据包（解码Hook入口）
     * 
     * @param frame 接收到的FEC帧
     * @param from_path_id 来源路径
     * @return 如果成功解码，返回恢复的原始数据
     */
    std::vector<std::vector<uint8_t>> receive_fec_frame(const FECFrame& frame,
                                                        uint32_t from_path_id);
    
    /**
     * @brief ACK反馈处理
     * 
     * 当收到ACK时调用，用于：
     * 1. 更新链路质量指标
     * 2. 触发OCO学习更新
     * 3. 调整FEC参数
     */
    void on_ack_received(uint32_t path_id, uint64_t packet_number, uint64_t rtt_us);
    
    /**
     * @brief 丢包通知处理
     */
    void on_packet_lost(uint32_t path_id, uint64_t packet_number);
    
    /**
     * @brief 定期更新（建议每100ms调用一次）
     * 
     * 执行：
     * - OCO决策更新
     * - FEC参数调整
     * - 编码组刷新
     */
    void periodic_update();
    
    /**
     * @brief 启用/禁用FEC
     */
    void set_fec_enabled(bool enabled);
    
    /**
     * @brief 设置FEC策略
     */
    void set_fec_strategy(AdaptiveFECStrategy::Strategy strategy);
    
    /**
     * @brief 获取统计信息
     */
    struct Statistics {
        uint64_t total_packets_sent;
        uint64_t source_packets_sent;
        uint64_t repair_packets_sent;
        uint64_t packets_recovered;
        uint64_t fec_groups_created;
        double current_redundancy_rate;
        double avg_encoding_time_us;
        
        Statistics() : total_packets_sent(0), source_packets_sent(0),
                      repair_packets_sent(0), packets_recovered(0),
                      fec_groups_created(0), current_redundancy_rate(0),
                      avg_encoding_time_us(0) {}
    };
    
    Statistics get_statistics() const { return stats_; }
    
    /**
     * @brief 获取路径调度器（用于外部查询）
     */
    std::shared_ptr<PathScheduler> get_path_scheduler() { return path_scheduler_; }
    
    /**
     * @brief 获取OCO控制器（用于外部配置）
     */
    std::shared_ptr<OCORedundancyController> get_oco_controller() { return oco_controller_; }

private:
    // 核心组件
    std::shared_ptr<FECGroupManager> group_manager_;
    std::shared_ptr<PacketSendHook> send_hook_;
    std::shared_ptr<PacketReceiveHook> receive_hook_;
    std::shared_ptr<PathScheduler> path_scheduler_;
    std::shared_ptr<OCORedundancyController> oco_controller_;
    std::shared_ptr<PacketNumberMapper> pkt_mapper_;
    std::shared_ptr<AdaptiveFECStrategy> fec_strategy_;
    
    // 当前冗余决策
    RedundancyDecision current_decision_;
    
    // 包序号生成器（每条路径独立）
    std::map<uint32_t, uint64_t> next_packet_numbers_;
    
    // 统计信息
    Statistics stats_;
    
    // 配置
    bool fec_enabled_;
    uint32_t block_size_;
    
    // 线程安全
    mutable std::mutex mutex_;
    
    // 上次更新时间
    uint64_t last_update_time_us_;
    
    /**
     * @brief 执行OCO决策并更新FEC参数
     */
    void update_fec_parameters();
    
    /**
     * @brief 分配包到路径
     */
    void assign_packets_to_paths(const std::vector<FECFrame>& frames,
                                 std::vector<SendPacketMeta>& out_packets);
    
    /**
     * @brief 获取下一个包序号
     */
    uint64_t get_next_packet_number(uint32_t path_id);
    
    /**
     * @brief 获取当前时间戳
     */
    uint64_t get_timestamp_us() const;
    
    /**
     * @brief 更新统计信息
     */
    void update_statistics(const std::vector<SendPacketMeta>& packets);
};

} // namespace mpquic_fec
