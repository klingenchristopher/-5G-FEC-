#include "path_scheduler.hpp"
#include "oco_controller.hpp"
#include "logger.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>

namespace mpquic_fec {

PathScheduler::PathScheduler() {
    LOG_INFO("PathScheduler initialized with OCO algorithm");
}

void PathScheduler::update_path_state(const PathState& state) {
    paths_[state.path_id] = state;
    
    // 如果是新路径，初始化权重
    if (weights_.find(state.path_id) == weights_.end()) {
        weights_[state.path_id] = 1.0 / std::max(1.0, static_cast<double>(paths_.size()));
        LOG_INFO("Added new path ", state.path_id, " with initial weight ", weights_[state.path_id]);
    }
    
    // 更新权重
    update_weights();
    
    LOG_DEBUG("Path ", state.path_id, " updated: RTT=", state.rtt_ms, "ms, Loss=", 
              state.loss_rate * 100, "%, BW=", state.bandwidth_mbps, "Mbps");
}

uint32_t PathScheduler::select_path(uint32_t packet_size) {
    if (paths_.empty()) {
        throw std::runtime_error("No paths available");
    }

    // 使用加权随机选择
    std::vector<uint32_t> path_ids;
    std::vector<double> cumulative_weights;
    double sum = 0.0;

    for (const auto& [path_id, weight] : weights_) {
        if (paths_.find(path_id) != paths_.end()) {
            path_ids.push_back(path_id);
            sum += weight;
            cumulative_weights.push_back(sum);
        }
    }

    // 归一化
    for (auto& w : cumulative_weights) {
        w /= sum;
    }

    // 随机选择
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    double r = dis(gen);

    for (size_t i = 0; i < cumulative_weights.size(); ++i) {
        if (r <= cumulative_weights[i]) {
            LOG_DEBUG("Selected path ", path_ids[i], " with weight ", weights_[path_ids[i]]);
            return path_ids[i];
        }
    }

    return path_ids.back();
}

uint32_t PathScheduler::select_source_path(uint32_t packet_size) {
    if (paths_.empty()) {
        throw std::runtime_error("No paths available");
    }
    
    // 为源包选择最优路径：优先考虑低RTT和低丢包率
    uint32_t best_path = paths_.begin()->first;
    double best_score = -1e9;
    
    for (const auto& [path_id, state] : paths_) {
        // 评分：低RTT + 低丢包率 + 高带宽
        double score = -0.4 * state.rtt_ms                   // RTT权重更高
                      -0.5 * state.loss_rate * 1000
                      + 0.1 * state.bandwidth_mbps;
        
        if (score > best_score) {
            best_score = score;
            best_path = path_id;
        }
    }
    
    LOG_DEBUG("Selected source path: ", best_path, " (score=", best_score, ")");
    return best_path;
}

uint32_t PathScheduler::select_repair_path(uint32_t source_path_id, uint32_t packet_size) {
    if (paths_.empty()) {
        throw std::runtime_error("No paths available");
    }
    
    if (paths_.size() == 1) {
        return paths_.begin()->first;  // 只有一条路径
    }
    
    // 策略：选择与源路径相关性最低的路径
    uint32_t repair_path = find_least_correlated_path(source_path_id);
    
    // 如果找到的路径与源路径相同（只有一条路径），则返回源路径
    if (repair_path == source_path_id && paths_.size() > 1) {
        // 选择除源路径外的第一条可用路径
        for (const auto& [path_id, _] : paths_) {
            if (path_id != source_path_id) {
                repair_path = path_id;
                break;
            }
        }
    }
    
    double correlation = get_path_correlation(source_path_id, repair_path);
    LOG_DEBUG("Selected repair path: ", repair_path, " for source path ", source_path_id,
              " (correlation=", correlation, ")");
    
    return repair_path;
}

void PathScheduler::set_oco_controller(std::shared_ptr<OCORedundancyController> controller) {
    oco_controller_ = controller;
    LOG_INFO("OCO controller attached to PathScheduler");
}

void PathScheduler::update_path_correlation(uint32_t path_i, uint32_t path_j, double correlation) {
    auto key = std::make_pair(std::min(path_i, path_j), std::max(path_i, path_j));
    path_correlations_[key] = correlation;
    
    // 如果有OCO控制器，同步更新
    if (oco_controller_) {
        oco_controller_->update_loss_correlation(path_i, path_j, correlation);
    }
    
    LOG_DEBUG("Updated path correlation: ", path_i, " <-> ", path_j, " = ", correlation);
}

bool PathScheduler::is_path_available(uint32_t path_id) const {
    auto it = paths_.find(path_id);
    if (it == paths_.end()) {
        return false;
    }
    
    // 检查路径是否处于可用状态（丢包率不太高，带宽充足）
    const auto& state = it->second;
    return state.loss_rate < 0.5 && state.bandwidth_mbps > 0.1;
}

double PathScheduler::get_path_correlation(uint32_t path_i, uint32_t path_j) const {
    if (path_i == path_j) {
        return 1.0;  // 自相关
    }
    
    auto key = std::make_pair(std::min(path_i, path_j), std::max(path_i, path_j));
    auto it = path_correlations_.find(key);
    if (it != path_correlations_.end()) {
        return it->second;
    }
    
    return 0.0;  // 默认假设独立
}

uint32_t PathScheduler::find_least_correlated_path(uint32_t path_id) const {
    if (paths_.size() <= 1) {
        return path_id;
    }
    
    double min_correlation = 2.0;
    uint32_t best_path = path_id;
    
    for (const auto& [candidate_id, _] : paths_) {
        if (candidate_id == path_id) {
            continue;
        }
        
        double corr = std::abs(get_path_correlation(path_id, candidate_id));
        if (corr < min_correlation) {
            min_correlation = corr;
            best_path = candidate_id;
        }
    }
    
    return best_path;
}

std::map<uint32_t, double> PathScheduler::get_path_weights() const {
    return weights_;
}

std::vector<PathState> PathScheduler::get_all_paths() const {
    std::vector<PathState> result;
    for (const auto& [_, state] : paths_) {
        result.push_back(state);
    }
    return result;
}

void PathScheduler::update_weights() {
    if (paths_.empty()) return;

    // 计算每条路径的代价
    std::map<uint32_t, double> costs;
    double total_cost = 0.0;

    for (const auto& [path_id, state] : paths_) {
        double cost = compute_cost(state);
        costs[path_id] = cost;
        total_cost += cost;
    }

    // 使用梯度下降更新权重（OCO算法核心）
    for (auto& [path_id, weight] : weights_) {
        if (costs.find(path_id) != costs.end()) {
            double gradient = costs[path_id] / std::max(0.001, total_cost);
            
            // 梯度下降
            weight = weight * std::exp(-alpha_ * gradient);
            
            // 确保权重为正
            weight = std::max(0.001, weight);
        }
    }

    // 归一化权重
    double sum = 0.0;
    for (const auto& [_, w] : weights_) {
        sum += w;
    }
    for (auto& [_, w] : weights_) {
        w /= sum;
    }

    LOG_DEBUG("Weights updated for ", weights_.size(), " paths");
}

double PathScheduler::compute_cost(const PathState& state) const {
    // 综合代价函数：考虑RTT、丢包率和带宽
    // cost = β * RTT + γ * loss_rate - δ * bandwidth
    
    double rtt_normalized = state.rtt_ms / 100.0;  // 归一化到合理范围
    double bw_normalized = 100.0 / std::max(1.0, state.bandwidth_mbps);  // 带宽的倒数
    
    double cost = beta_ * rtt_normalized + 
                  gamma_ * state.loss_rate +
                  delta_ * bw_normalized;
    
    return std::max(0.001, cost);  // 避免零代价
}

} // namespace mpquic_fec
