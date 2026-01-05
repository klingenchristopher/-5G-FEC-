#pragma once

#include <vector>
#include <cstdint>
#include <map>
#include <memory>

namespace mpquic_fec {

// Forward declaration
struct FECFrame;
class OCORedundancyController;

/**
 * @brief 路径状态信息
 */
struct PathState {
    uint32_t path_id;
    double rtt_ms;           // 往返时延 (毫秒)
    double loss_rate;        // 丢包率 [0, 1]
    double bandwidth_mbps;   // 带宽 (Mbps)
    uint64_t bytes_sent;     // 已发送字节数
    uint64_t bytes_acked;    // 已确认字节数
    double jitter_ms;        // 抖动 (毫秒)
    uint64_t cwnd;           // 拥塞窗口
    
    PathState() : path_id(0), rtt_ms(0), loss_rate(0), bandwidth_mbps(0),
                  bytes_sent(0), bytes_acked(0), jitter_ms(0), cwnd(0) {}
};

/**
 * @brief 基于在线凸优化(OCO)的路径调度器
 * 
 * 目标：最小化延迟和丢包率，同时最大化吞吐量
 * 使用梯度下降方法动态调整路径权重
 * 
 * 增强功能：
 * - 支持跨路径FEC调度
 * - 集成OCO动态冗余决策
 * - 支持Source/Repair包的差异化路径选择
 */
class PathScheduler {
public:
    PathScheduler();
    ~PathScheduler() = default;

    /**
     * @brief 更新路径状态
     */
    void update_path_state(const PathState& state);

    /**
     * @brief 选择下一个发送数据包的路径
     * @param packet_size 数据包大小（字节）
     * @return 选中的路径ID
     */
    uint32_t select_path(uint32_t packet_size);
    
    /**
     * @brief 为FEC源包选择最优路径
     * @return 源包路径ID
     */
    uint32_t select_source_path(uint32_t packet_size);
    
    /**
     * @brief 为FEC冗余包选择最优路径
     * @param source_path_id 源包使用的路径ID
     * @return 冗余包路径ID（优先选择与源路径相关性低的路径）
     */
    uint32_t select_repair_path(uint32_t source_path_id, uint32_t packet_size);

    /**
     * @brief 计算每条路径应分配的数据比例
     * @return path_id -> 权重 (0到1之间，总和为1)
     */
    std::map<uint32_t, double> get_path_weights() const;

    /**
     * @brief 获取路径统计信息
     */
    std::vector<PathState> get_all_paths() const;
    
    /**
     * @brief 设置OCO控制器（用于集成动态冗余决策）
     */
    void set_oco_controller(std::shared_ptr<OCORedundancyController> controller);
    
    /**
     * @brief 更新路径间的丢包相关性
     */
    void update_path_correlation(uint32_t path_i, uint32_t path_j, double correlation);
    
    /**
     * @brief 检查路径是否可用
     */
    bool is_path_available(uint32_t path_id) const;

private:
    std::map<uint32_t, PathState> paths_;
    std::map<uint32_t, double> weights_;  // 当前路径权重
    
    // OCO控制器（可选集成）
    std::shared_ptr<OCORedundancyController> oco_controller_;
    
    // 路径间丢包相关性矩阵（简化版）
    std::map<std::pair<uint32_t, uint32_t>, double> path_correlations_;
    
    double alpha_ = 0.1;  // 学习率
    double beta_ = 0.5;   // RTT权重系数
    double gamma_ = 0.3;  // 丢包率权重系数
    double delta_ = 0.2;  // 带宽权重系数

    /**
     * @brief 使用OCO算法更新权重
     */
    void update_weights();

    /**
     * @brief 计算路径代价函数
     */
    double compute_cost(const PathState& state) const;
    
    /**
     * @brief 获取路径间的丢包相关性
     */
    double get_path_correlation(uint32_t path_i, uint32_t path_j) const;
    
    /**
     * @brief 找到与指定路径相关性最低的路径
     */
    uint32_t find_least_correlated_path(uint32_t path_id) const;
};

} // namespace mpquic_fec
