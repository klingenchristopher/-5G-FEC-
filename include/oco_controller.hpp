#pragma once

#include "path_scheduler.hpp"
#include <cstdint>
#include <map>
#include <vector>

namespace mpquic_fec {

/**
 * @brief 链路质量指标
 */
struct LinkMetrics {
    uint32_t path_id;
    double rtt_ms;               // 往返时延
    double loss_rate;            // 丢包率
    double bandwidth_mbps;       // 带宽
    double jitter_ms;            // 抖动
    uint64_t packets_sent;       // 发送包数
    uint64_t packets_lost;       // 丢失包数
    uint64_t bytes_in_flight;    // 在途字节数
    
    LinkMetrics() 
        : path_id(0), rtt_ms(0), loss_rate(0), bandwidth_mbps(0), 
          jitter_ms(0), packets_sent(0), packets_lost(0), bytes_in_flight(0) {}
};

/**
 * @brief 路径间丢包相关性
 * 
 * ρ(i,j) 表示路径i和路径j之间的丢包相关系数
 * ρ ∈ [-1, 1], 接近1表示强正相关，接近-1表示负相关，0表示独立
 */
class LossCorrelationMatrix {
public:
    LossCorrelationMatrix() = default;
    
    /**
     * @brief 更新路径间的丢包相关性
     */
    void update_correlation(uint32_t path_i, uint32_t path_j, double rho);
    
    /**
     * @brief 获取路径间的丢包相关性
     */
    double get_correlation(uint32_t path_i, uint32_t path_j) const;
    
    /**
     * @brief 找到与给定路径相关性最低的路径
     * @return 相关性最低的路径ID
     */
    uint32_t find_least_correlated_path(uint32_t path_id, 
                                        const std::vector<uint32_t>& available_paths) const;
    
private:
    // 相关性矩阵：(path_i, path_j) -> ρ
    std::map<std::pair<uint32_t, uint32_t>, double> correlation_matrix_;
};

/**
 * @brief FEC冗余度决策结果
 */
struct RedundancyDecision {
    uint32_t k;                  // 源包数量
    uint32_t m;                  // 冗余包数量
    double redundancy_rate;      // 冗余率 m/k
    uint32_t source_path;        // 源包发送路径
    uint32_t repair_path;        // 冗余包发送路径
    double confidence;           // 决策置信度 [0, 1]
    
    RedundancyDecision() 
        : k(4), m(2), redundancy_rate(0.5), source_path(0), 
          repair_path(1), confidence(1.0) {}
};

/**
 * @brief OCO (Online Convex Optimization) 动态冗余决策器
 * 
 * 核心目标：
 * 1. 根据实时链路质量动态调整FEC参数 (k, m)
 * 2. 基于路径间丢包相关性进行跨路径冗余分配
 * 3. 实现毫秒级响应的"按需冗余"机制
 * 
 * 代价函数：
 * Cost = α₁·Loss + α₂·Delay + α₃·Overhead
 * 
 * 约束条件：
 * - m/k ∈ [0.1, 1.0] (冗余率范围)
 * - Total_Overhead < Bandwidth * β (带宽限制)
 */
class OCORedundancyController {
public:
    OCORedundancyController();
    ~OCORedundancyController() = default;
    
    /**
     * @brief 更新链路质量指标
     */
    void update_link_metrics(const LinkMetrics& metrics);
    
    /**
     * @brief 更新路径间丢包相关性
     */
    void update_loss_correlation(uint32_t path_i, uint32_t path_j, double rho);
    
    /**
     * @brief 计算最优FEC冗余度
     * 
     * 基于当前所有路径的状态，使用OCO算法计算最优的(k, m)参数
     * 
     * @return 冗余决策结果
     */
    RedundancyDecision compute_optimal_redundancy();
    
    /**
     * @brief 根据ACK反馈更新决策参数
     * 
     * @param actual_loss 实际丢包率
     * @param actual_rtt 实际RTT
     */
    void feedback_update(double actual_loss, double actual_rtt);
    
    /**
     * @brief 设置代价函数权重
     */
    void set_cost_weights(double loss_weight, double delay_weight, double overhead_weight);
    
    /**
     * @brief 设置冗余率约束
     */
    void set_redundancy_constraints(double min_rate, double max_rate);
    
    /**
     * @brief 获取当前所有路径的指标
     */
    std::vector<LinkMetrics> get_all_metrics() const;
    
private:
    // 链路质量指标缓存
    std::map<uint32_t, LinkMetrics> link_metrics_;
    
    // 丢包相关性矩阵
    LossCorrelationMatrix correlation_matrix_;
    
    // 代价函数权重
    double alpha_loss_;      // 丢包权重
    double alpha_delay_;     // 延迟权重
    double alpha_overhead_;  // 开销权重
    
    // 冗余率约束
    double min_redundancy_rate_;
    double max_redundancy_rate_;
    
    // OCO学习参数
    double learning_rate_;           // 学习率 η
    std::map<uint32_t, double> gradient_accumulator_;  // 梯度累积
    
    // 历史决策记录（用于在线学习）
    struct DecisionHistory {
        RedundancyDecision decision;
        double actual_loss;
        double actual_cost;
        uint64_t timestamp_us;
    };
    std::vector<DecisionHistory> history_;
    size_t max_history_size_;
    
    /**
     * @brief 计算给定冗余配置的代价
     */
    double compute_cost(uint32_t k, uint32_t m, 
                       const LinkMetrics& source_metrics,
                       const LinkMetrics& repair_metrics) const;
    
    /**
     * @brief 计算代价函数的梯度
     */
    double compute_gradient(uint32_t path_id) const;
    
    /**
     * @brief 选择最优的源包路径
     */
    uint32_t select_source_path() const;
    
    /**
     * @brief 选择最优的冗余包路径
     * @param source_path 已选择的源包路径
     */
    uint32_t select_repair_path(uint32_t source_path) const;
    
    /**
     * @brief 根据链路质量计算建议的冗余率
     */
    double estimate_required_redundancy(const LinkMetrics& metrics) const;
    
    /**
     * @brief 将冗余率转换为(k,m)参数
     */
    std::pair<uint32_t, uint32_t> redundancy_rate_to_params(double rate) const;
    
    /**
     * @brief 使用梯度下降更新学习参数
     */
    void gradient_descent_update(double actual_loss, double predicted_loss);
};

/**
 * @brief 自适应FEC策略选择器
 * 
 * 在不同网络条件下选择不同的FEC策略
 */
class AdaptiveFECStrategy {
public:
    enum class Strategy {
        AGGRESSIVE,      // 激进策略：高冗余率，适用于恶劣网络
        BALANCED,        // 平衡策略：中等冗余率
        CONSERVATIVE,    // 保守策略：低冗余率，适用于良好网络
        DYNAMIC          // 动态策略：完全由OCO决定
    };
    
    AdaptiveFECStrategy();
    
    /**
     * @brief 根据网络状态选择策略
     */
    Strategy select_strategy(const std::vector<LinkMetrics>& metrics) const;
    
    /**
     * @brief 获取策略对应的参数范围
     */
    std::pair<double, double> get_strategy_redundancy_range(Strategy strategy) const;
    
private:
    // 策略切换阈值
    double aggressive_loss_threshold_;
    double conservative_loss_threshold_;
};

} // namespace mpquic_fec
