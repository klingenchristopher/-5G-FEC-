#pragma once

#include <vector>
#include <cstdint>
#include <map>

namespace mpquic_fec {

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
};

/**
 * @brief 基于在线凸优化(OCO)的路径调度器
 * 
 * 目标：最小化延迟和丢包率，同时最大化吞吐量
 * 使用梯度下降方法动态调整路径权重
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
     * @brief 计算每条路径应分配的数据比例
     * @return path_id -> 权重 (0到1之间，总和为1)
     */
    std::map<uint32_t, double> get_path_weights() const;

    /**
     * @brief 获取路径统计信息
     */
    std::vector<PathState> get_all_paths() const;

private:
    std::map<uint32_t, PathState> paths_;
    std::map<uint32_t, double> weights_;  // 当前路径权重
    
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
};

} // namespace mpquic_fec
