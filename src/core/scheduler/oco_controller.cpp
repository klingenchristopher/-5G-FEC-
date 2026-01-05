#include "oco_controller.hpp"
#include "logger.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>

namespace mpquic_fec {

// ========== LossCorrelationMatrix 实现 ==========

void LossCorrelationMatrix::update_correlation(uint32_t path_i, uint32_t path_j, double rho) {
    // 确保相关性在[-1, 1]范围内
    rho = std::max(-1.0, std::min(1.0, rho));
    
    auto key1 = std::make_pair(std::min(path_i, path_j), std::max(path_i, path_j));
    correlation_matrix_[key1] = rho;
    
    LOG_DEBUG("Updated loss correlation: Path ", path_i, " <-> Path ", path_j, 
              ", ρ = ", rho);
}

double LossCorrelationMatrix::get_correlation(uint32_t path_i, uint32_t path_j) const {
    if (path_i == path_j) {
        return 1.0;  // 自相关为1
    }
    
    auto key = std::make_pair(std::min(path_i, path_j), std::max(path_i, path_j));
    auto it = correlation_matrix_.find(key);
    if (it != correlation_matrix_.end()) {
        return it->second;
    }
    
    return 0.0;  // 默认假设独立（无相关性）
}

uint32_t LossCorrelationMatrix::find_least_correlated_path(
    uint32_t path_id, const std::vector<uint32_t>& available_paths) const {
    
    if (available_paths.empty()) {
        return path_id;
    }
    
    double min_correlation = 2.0;  // 初始值大于最大可能相关性
    uint32_t best_path = available_paths[0];
    
    for (uint32_t candidate : available_paths) {
        if (candidate == path_id) {
            continue;
        }
        
        double corr = std::abs(get_correlation(path_id, candidate));
        if (corr < min_correlation) {
            min_correlation = corr;
            best_path = candidate;
        }
    }
    
    LOG_DEBUG("Least correlated path for ", path_id, " is ", best_path,
              " (ρ = ", min_correlation, ")");
    
    return best_path;
}

// ========== OCORedundancyController 实现 ==========

OCORedundancyController::OCORedundancyController()
    : alpha_loss_(0.5), alpha_delay_(0.3), alpha_overhead_(0.2),
      min_redundancy_rate_(0.1), max_redundancy_rate_(1.0),
      learning_rate_(0.05), max_history_size_(100) {
    
    LOG_INFO("OCORedundancyController initialized");
    LOG_INFO("  Loss weight: ", alpha_loss_);
    LOG_INFO("  Delay weight: ", alpha_delay_);
    LOG_INFO("  Overhead weight: ", alpha_overhead_);
}

void OCORedundancyController::update_link_metrics(const LinkMetrics& metrics) {
    link_metrics_[metrics.path_id] = metrics;
    
    LOG_DEBUG("Updated metrics for Path ", metrics.path_id,
              ": RTT=", metrics.rtt_ms, "ms, Loss=", metrics.loss_rate * 100, "%");
}

void OCORedundancyController::update_loss_correlation(uint32_t path_i, uint32_t path_j, double rho) {
    correlation_matrix_.update_correlation(path_i, path_j, rho);
}

RedundancyDecision OCORedundancyController::compute_optimal_redundancy() {
    if (link_metrics_.empty()) {
        LOG_WARN("No link metrics available, using default redundancy");
        return RedundancyDecision();
    }
    
    RedundancyDecision decision;
    
    // 1. 选择源包路径（选择RTT最低且丢包率可接受的路径）
    decision.source_path = select_source_path();
    
    // 2. 选择冗余包路径（选择与源路径相关性最低的路径）
    decision.repair_path = select_repair_path(decision.source_path);
    
    // 3. 根据链路质量估算所需冗余率
    const auto& source_metrics = link_metrics_[decision.source_path];
    double required_redundancy = estimate_required_redundancy(source_metrics);
    
    // 4. 应用约束
    decision.redundancy_rate = std::max(min_redundancy_rate_, 
                                       std::min(max_redundancy_rate_, required_redundancy));
    
    // 5. 转换为(k, m)参数
    auto [k, m] = redundancy_rate_to_params(decision.redundancy_rate);
    decision.k = k;
    decision.m = m;
    
    // 6. 计算决策置信度
    decision.confidence = 1.0 - source_metrics.loss_rate;
    
    // 7. 计算并记录代价
    const auto& repair_metrics = link_metrics_[decision.repair_path];
    double cost = compute_cost(k, m, source_metrics, repair_metrics);
    
    LOG_INFO("OCO Decision: k=", k, ", m=", m, ", redundancy=", 
             decision.redundancy_rate * 100, "%, cost=", cost);
    LOG_INFO("  Source Path: ", decision.source_path, 
             " (RTT=", source_metrics.rtt_ms, "ms, Loss=", 
             source_metrics.loss_rate * 100, "%)");
    LOG_INFO("  Repair Path: ", decision.repair_path,
             " (correlation=", correlation_matrix_.get_correlation(
                 decision.source_path, decision.repair_path), ")");
    
    return decision;
}

void OCORedundancyController::feedback_update(double actual_loss, double actual_rtt) {
    if (history_.empty()) {
        return;
    }
    
    // 获取最近的决策
    auto& last_decision = history_.back();
    last_decision.actual_loss = actual_loss;
    
    // 计算预测误差
    double predicted_loss = link_metrics_[last_decision.decision.source_path].loss_rate;
    double error = actual_loss - predicted_loss;
    
    // 使用梯度下降更新学习参数
    gradient_descent_update(actual_loss, predicted_loss);
    
    LOG_DEBUG("Feedback update: Predicted loss=", predicted_loss * 100,
              "%, Actual loss=", actual_loss * 100, "%, Error=", error * 100, "%");
}

void OCORedundancyController::set_cost_weights(double loss_weight, double delay_weight, 
                                               double overhead_weight) {
    alpha_loss_ = loss_weight;
    alpha_delay_ = delay_weight;
    alpha_overhead_ = overhead_weight;
    
    // 归一化
    double sum = alpha_loss_ + alpha_delay_ + alpha_overhead_;
    alpha_loss_ /= sum;
    alpha_delay_ /= sum;
    alpha_overhead_ /= sum;
    
    LOG_INFO("Updated cost weights: Loss=", alpha_loss_, 
             ", Delay=", alpha_delay_, ", Overhead=", alpha_overhead_);
}

void OCORedundancyController::set_redundancy_constraints(double min_rate, double max_rate) {
    min_redundancy_rate_ = std::max(0.0, min_rate);
    max_redundancy_rate_ = std::min(1.0, max_rate);
    
    LOG_INFO("Updated redundancy constraints: [", min_redundancy_rate_, ", ",
             max_redundancy_rate_, "]");
}

std::vector<LinkMetrics> OCORedundancyController::get_all_metrics() const {
    std::vector<LinkMetrics> result;
    for (const auto& [_, metrics] : link_metrics_) {
        result.push_back(metrics);
    }
    return result;
}

double OCORedundancyController::compute_cost(uint32_t k, uint32_t m,
                                             const LinkMetrics& source_metrics,
                                             const LinkMetrics& repair_metrics) const {
    // 归一化丢包率损失
    double loss_cost = source_metrics.loss_rate;
    
    // 归一化延迟损失（假设最大RTT为500ms）
    double delay_cost = (source_metrics.rtt_ms + repair_metrics.rtt_ms) / 1000.0;
    
    // 归一化开销损失（冗余率作为开销）
    double overhead_cost = static_cast<double>(m) / static_cast<double>(k);
    
    // 综合代价
    double total_cost = alpha_loss_ * loss_cost +
                       alpha_delay_ * delay_cost +
                       alpha_overhead_ * overhead_cost;
    
    return total_cost;
}

double OCORedundancyController::compute_gradient(uint32_t path_id) const {
    auto it = link_metrics_.find(path_id);
    if (it == link_metrics_.end()) {
        return 0.0;
    }
    
    const auto& metrics = it->second;
    
    // 简化的梯度计算：基于丢包率和RTT的加权组合
    double gradient = alpha_loss_ * metrics.loss_rate +
                     alpha_delay_ * (metrics.rtt_ms / 100.0);
    
    return gradient;
}

uint32_t OCORedundancyController::select_source_path() const {
    if (link_metrics_.empty()) {
        return 0;
    }
    
    // 选择综合评分最高的路径（低RTT + 低丢包率 + 高带宽）
    double best_score = -1e9;
    uint32_t best_path = link_metrics_.begin()->first;
    
    for (const auto& [path_id, metrics] : link_metrics_) {
        // 评分函数：综合考虑RTT、丢包率和带宽
        double score = -0.3 * metrics.rtt_ms                    // RTT越低越好
                      -0.5 * metrics.loss_rate * 1000          // 丢包率越低越好
                      + 0.2 * metrics.bandwidth_mbps;          // 带宽越高越好
        
        if (score > best_score) {
            best_score = score;
            best_path = path_id;
        }
    }
    
    return best_path;
}

uint32_t OCORedundancyController::select_repair_path(uint32_t source_path) const {
    std::vector<uint32_t> available_paths;
    for (const auto& [path_id, _] : link_metrics_) {
        if (path_id != source_path) {
            available_paths.push_back(path_id);
        }
    }
    
    if (available_paths.empty()) {
        return source_path;  // 只有一条路径
    }
    
    // 选择与源路径相关性最低的路径
    return correlation_matrix_.find_least_correlated_path(source_path, available_paths);
}

double OCORedundancyController::estimate_required_redundancy(const LinkMetrics& metrics) const {
    // 基于丢包率估算所需冗余率
    // 使用线性模型：redundancy = min(loss_rate * 2.0, max_rate)
    // 如果丢包率10%，建议冗余率20%
    
    double base_redundancy = metrics.loss_rate * 2.0;
    
    // 考虑RTT影响：高RTT需要更多冗余以应对重传延迟
    double rtt_factor = 1.0 + (metrics.rtt_ms / 200.0) * 0.3;
    
    double required = base_redundancy * rtt_factor;
    
    return std::max(min_redundancy_rate_, 
                   std::min(max_redundancy_rate_, required));
}

std::pair<uint32_t, uint32_t> OCORedundancyController::redundancy_rate_to_params(double rate) const {
    // 将冗余率转换为合理的(k, m)组合
    // 保持k在[4, 16]范围内以平衡性能和延迟
    
    uint32_t k = 8;  // 默认k值
    uint32_t m = static_cast<uint32_t>(std::ceil(k * rate));
    
    // 根据冗余率调整k值
    if (rate < 0.2) {
        k = 10;  // 低冗余率时使用较大的k
    } else if (rate > 0.6) {
        k = 4;   // 高冗余率时使用较小的k以减少延迟
    }
    
    m = static_cast<uint32_t>(std::ceil(k * rate));
    m = std::max(1u, std::min(m, k));  // 确保m在[1, k]范围内
    
    return {k, m};
}

void OCORedundancyController::gradient_descent_update(double actual_loss, double predicted_loss) {
    // 计算预测误差
    double error = actual_loss - predicted_loss;
    
    // 更新所有路径的梯度累积器
    for (auto& [path_id, accum] : gradient_accumulator_) {
        double gradient = compute_gradient(path_id);
        
        // 梯度下降更新：accumulator = accumulator - η * gradient * error
        accum -= learning_rate_ * gradient * error;
    }
    
    LOG_DEBUG("Gradient descent update: error=", error * 100, "%");
}

// ========== AdaptiveFECStrategy 实现 ==========

AdaptiveFECStrategy::AdaptiveFECStrategy()
    : aggressive_loss_threshold_(0.15),      // 丢包率 > 15% 使用激进策略
      conservative_loss_threshold_(0.02) {   // 丢包率 < 2% 使用保守策略
    
    LOG_INFO("AdaptiveFECStrategy initialized");
}

AdaptiveFECStrategy::Strategy AdaptiveFECStrategy::select_strategy(
    const std::vector<LinkMetrics>& metrics) const {
    
    if (metrics.empty()) {
        return Strategy::BALANCED;
    }
    
    // 计算平均丢包率
    double avg_loss = 0.0;
    double max_loss = 0.0;
    
    for (const auto& m : metrics) {
        avg_loss += m.loss_rate;
        max_loss = std::max(max_loss, m.loss_rate);
    }
    avg_loss /= metrics.size();
    
    // 根据丢包率选择策略
    if (max_loss > aggressive_loss_threshold_) {
        LOG_INFO("Selected AGGRESSIVE strategy (max_loss=", max_loss * 100, "%)");
        return Strategy::AGGRESSIVE;
    } else if (avg_loss < conservative_loss_threshold_) {
        LOG_INFO("Selected CONSERVATIVE strategy (avg_loss=", avg_loss * 100, "%)");
        return Strategy::CONSERVATIVE;
    } else {
        LOG_INFO("Selected BALANCED strategy (avg_loss=", avg_loss * 100, "%)");
        return Strategy::BALANCED;
    }
}

std::pair<double, double> AdaptiveFECStrategy::get_strategy_redundancy_range(
    Strategy strategy) const {
    
    switch (strategy) {
        case Strategy::AGGRESSIVE:
            return {0.4, 1.0};   // 40%-100% 冗余率
        case Strategy::CONSERVATIVE:
            return {0.1, 0.3};   // 10%-30% 冗余率
        case Strategy::BALANCED:
            return {0.2, 0.6};   // 20%-60% 冗余率
        case Strategy::DYNAMIC:
        default:
            return {0.1, 1.0};   // 完全动态范围
    }
}

} // namespace mpquic_fec
