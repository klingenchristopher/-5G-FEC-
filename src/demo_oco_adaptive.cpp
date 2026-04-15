/**
 * @file demo_oco_adaptive.cpp
 * @brief OCO自适应FEC仿真实验 — 动态5G信道
 *
 * 本演示程序模拟200个时间步（每步≈1秒）的5G信道，分三个阶段：
 *   阶段1 (0–59s)   良好信道   : 丢包率 ~2–4%
 *   阶段2 (60–139s) 信道恶化   : 丢包率 ~10–25%，含突发
 *   阶段3 (140–199s) 信道恢复  : 丢包率 ~2–4%
 *
 * 比较三种策略：
 *   1. OCO自适应FEC : 调用 OCORedundancyController 动态决定 (k, m)
 *   2. 静态FEC      : 固定 k=4, m=2（50% 冗余率），面向"平均"信道
 *   3. 无FEC        : 不使用任何前向纠错
 *
 * 输出：
 *   - 控制台：逐步进度表 + 汇总统计
 *   - CSV：experiments/results/oco_simulation_results.csv（相对于运行目录）
 *
 * 运行方式（从仓库根目录）：
 *   cd build && cmake .. && make -j$(nproc) && cd ..
 *   ./build/bin/demo_oco_adaptive
 *   python3 experiments/scripts/plot_oco_results.py
 */

#include "oco_controller.hpp"
#include "logger.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

using namespace mpquic_fec;

// ── Simulation constants ────────────────────────────────────────────────────

static constexpr int    SIM_STEPS    = 200;  // number of 1-second time slots
// N_GROUPS: Monte Carlo sample size per slot. 500 groups yields ±2 pp accuracy
// at 95% CI for recovery rates in the 90–100% range (σ ≈ √(p(1-p)/N) ≤ 0.02).
static constexpr int    N_GROUPS     = 500;
static constexpr double STATIC_K     = 4.0;  // static FEC data blocks
static constexpr double STATIC_M     = 2.0;  // static FEC parity blocks (50% overhead)

// ── Channel model ────────────────────────────────────────────────────────────

struct ChannelState {
    int    step;
    int    phase;        // 1=good, 2=degraded, 3=recovery
    double path0_rtt;    // ms
    double path1_rtt;    // ms
    double path0_loss;   // [0, 1]
    double path1_loss;   // [0, 1]
    double correlation;  // loss correlation coefficient ρ ∈ [-1, 1]
};

static double clamp(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}

static double lerp(double a, double b, double t) {
    return a + (b - a) * clamp(t, 0.0, 1.0);
}

/**
 * @brief 生成第 t 步的信道状态（含噪声，使用确定性随机源）
 *
 * 三阶段模型：
 *   Phase 1 (t=0..59)   : 良好信道，低RTT，低丢包率
 *   Phase 2 (t=60..139) : 逐渐恶化（60-79 爬坡），持续高丢包（80-119），
 *                          含周期性突发（burst），然后开始恢复（120-139）
 *   Phase 3 (t=140..199): 恢复至良好水平
 */
static ChannelState get_channel(int t, std::mt19937& rng) {
    std::normal_distribution<double> rtt_noise(0.0, 3.0);
    std::normal_distribution<double> loss_noise(0.0, 0.005);

    ChannelState cs;
    cs.step = t;
    double rn  = rtt_noise(rng);
    double ln  = loss_noise(rng);

    if (t < 60) {
        // ── Phase 1: good channel ────────────────────────────────────────
        cs.phase = 1;
        cs.path0_rtt  = clamp(25.0 + 5.0 * std::sin(2*M_PI*t/30.0) + rn,   5.0, 200.0);
        cs.path1_rtt  = clamp(40.0 + 8.0 * std::sin(2*M_PI*t/25.0) + rn*0.8, 5.0, 200.0);
        cs.path0_loss = clamp(0.030 + 0.008 * std::sin(2*M_PI*t/20.0) + ln,   0.001, 0.40);
        cs.path1_loss = clamp(0.012 + 0.005 * std::sin(2*M_PI*t/15.0) + ln*0.6, 0.001, 0.40);
        cs.correlation = 0.05;

    } else if (t < 80) {
        // ── Phase 2a: ramp-up ────────────────────────────────────────────
        cs.phase = 2;
        double alpha  = (t - 60) / 20.0;
        cs.path0_rtt  = clamp(lerp(25.0, 80.0, alpha) + 6.0*std::sin(2*M_PI*t/15.0) + rn,     5.0, 200.0);
        cs.path1_rtt  = clamp(lerp(40.0, 65.0, alpha) + 5.0*std::sin(2*M_PI*t/18.0) + rn*0.8, 5.0, 200.0);
        cs.path0_loss = clamp(lerp(0.030, 0.220, alpha) + ln,       0.001, 0.50);
        cs.path1_loss = clamp(lerp(0.012, 0.120, alpha) + ln*0.6,   0.001, 0.50);
        cs.correlation = lerp(0.05, 0.35, alpha);

    } else if (t < 140) {
        // ── Phase 2b: steady degraded + periodic bursts ──────────────────
        cs.phase = 2;
        double burst  = (std::sin(2*M_PI*(t-80)/12.0) > 0.65) ? 0.07 : 0.0;
        cs.path0_rtt  = clamp(80.0 + 10.0*std::sin(2*M_PI*t/12.0) + rn*1.5, 5.0, 200.0);
        cs.path1_rtt  = clamp(65.0 +  8.0*std::sin(2*M_PI*t/15.0) + rn,     5.0, 200.0);
        cs.path0_loss = clamp(0.200 + 0.055*std::sin(2*M_PI*t/8.0)  + burst + ln,       0.05, 0.55);
        cs.path1_loss = clamp(0.110 + 0.040*std::sin(2*M_PI*t/10.0) + burst*0.5 + ln*0.6, 0.02, 0.35);
        cs.correlation = clamp(0.35 + 0.10*std::sin(2*M_PI*t/15.0), -1.0, 1.0);

    } else if (t < 165) {
        // ── Phase 3a: ramp-down (recovery) ──────────────────────────────
        cs.phase = 3;
        double alpha  = (t - 140) / 25.0;
        cs.path0_rtt  = clamp(lerp(80.0, 25.0, alpha) + 5.0*std::sin(2*M_PI*t/20.0) + rn,     5.0, 200.0);
        cs.path1_rtt  = clamp(lerp(65.0, 40.0, alpha) + 6.0*std::sin(2*M_PI*t/22.0) + rn*0.8, 5.0, 200.0);
        cs.path0_loss = clamp(lerp(0.200, 0.030, alpha) + ln,       0.001, 0.50);
        cs.path1_loss = clamp(lerp(0.110, 0.012, alpha) + ln*0.6,   0.001, 0.50);
        cs.correlation = lerp(0.35, 0.05, alpha);

    } else {
        // ── Phase 3b: stable recovery ────────────────────────────────────
        cs.phase = 3;
        cs.path0_rtt  = clamp(25.0 + 5.0*std::sin(2*M_PI*t/20.0) + rn,     5.0, 200.0);
        cs.path1_rtt  = clamp(40.0 + 7.0*std::sin(2*M_PI*t/25.0) + rn*0.8, 5.0, 200.0);
        cs.path0_loss = clamp(0.030 + 0.008*std::sin(2*M_PI*t/28.0) + ln,       0.001, 0.40);
        cs.path1_loss = clamp(0.012 + 0.004*std::sin(2*M_PI*t/22.0) + ln*0.6,   0.001, 0.40);
        cs.correlation = 0.05;
    }

    return cs;
}

// ── Monte-Carlo FEC recovery simulation ─────────────────────────────────────

/**
 * @brief 蒙特卡洛模拟 FEC 恢复率
 *
 * 对 n_groups 个 FEC 编码组分别仿真：
 *   每组共 k+m 个包，每个包以概率 loss_rate 独立丢失
 *   若收到包数 ≥ k，则认为该组成功恢复
 *
 * @return 恢复成功率 [0, 1]
 */
static double simulate_recovery(uint32_t k, uint32_t m, double loss_rate,
                                 int n_groups, std::mt19937& rng) {
    if (k == 0 || m > k * 4) return 0.0;  // guard against degenerate parameters
    uint32_t n = k + m;
    std::bernoulli_distribution drop(loss_rate);
    int ok = 0;
    for (int g = 0; g < n_groups; ++g) {
        uint32_t recv = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (!drop(rng)) ++recv;
        }
        if (recv >= k) ++ok;
    }
    return static_cast<double>(ok) / n_groups;
}

// ── Per-step result record ────────────────────────────────────────────────────

struct StepResult {
    int    step, phase;
    double path0_rtt, path1_rtt;
    double path0_loss_pct, path1_loss_pct;
    double correlation;
    uint32_t oco_k, oco_m;
    double oco_redundancy_pct;
    double oco_effective_rate;   // k / (k+m)
    double oco_recovery_pct;
    uint32_t static_k, static_m;
    double static_redundancy_pct;
    double static_effective_rate;
    double static_recovery_pct;
    double nofec_recovery_pct;
    double best_path_loss_pct;   // loss on OCO-selected source path
};

// ── Printing helpers ─────────────────────────────────────────────────────────

static void print_banner() {
    std::cout
        << "\n"
        << "╔══════════════════════════════════════════════════════════════════╗\n"
        << "║    OCO 自适应 FEC 仿真实验 — 动态 5G 信道                      ║\n"
        << "║    在线凸优化(OCO)驱动的跨路径冗余自适应调整                   ║\n"
        << "╚══════════════════════════════════════════════════════════════════╝\n"
        << "\n"
        << "  仿真场景（共 " << SIM_STEPS << " 步，每步 = 1 秒）：\n"
        << "    阶段1  (0–59s)   : 良好信道，丢包率 ~2–4%\n"
        << "    阶段2  (60–139s) : 信道恶化，丢包率 ~10–25%（含突发）\n"
        << "    阶段3  (140–199s): 信道恢复，丢包率 ~2–4%\n"
        << "\n"
        << "  对比策略：\n"
        << "    [OCO]    自适应 FEC — 每步由 OCO 算法决定 (k, m)\n"
        << "    [Static] 固定 FEC   — k=4, m=2（冗余率固定 50%）\n"
        << "    [NoFEC]  无冗余     — 不使用 FEC\n"
        << "\n";
}

static void print_table_header() {
    std::cout
        << std::setw(5)  << "Step"
        << std::setw(7)  << "Phase"
        << std::setw(9)  << "Loss0%"
        << std::setw(9)  << "Loss1%"
        << std::setw(7)  << "OCO k"
        << std::setw(7)  << "OCO m"
        << std::setw(11) << "OCO Ovhd"
        << std::setw(13) << "OCO Recv%"
        << std::setw(13) << "Stat Recv%"
        << std::setw(13) << "NoFEC Recv%"
        << "\n"
        << std::string(94, '-') << "\n";
}

static void print_table_row(const StepResult& r) {
    const char* phase_str =
        (r.phase == 1) ? "Good  " :
        (r.phase == 2) ? "Degrad" : "Recov ";
    std::cout
        << std::setw(5)  << r.step
        << std::setw(7)  << phase_str
        << std::fixed << std::setprecision(1)
        << std::setw(8)  << r.path0_loss_pct << "%"
        << std::setw(8)  << r.path1_loss_pct << "%"
        << std::setw(7)  << r.oco_k
        << std::setw(7)  << r.oco_m
        << std::setw(10) << r.oco_redundancy_pct << "%"
        << std::setw(12) << r.oco_recovery_pct   << "%"
        << std::setw(12) << r.static_recovery_pct << "%"
        << std::setw(12) << r.nofec_recovery_pct  << "%"
        << "\n";
}

static void print_summary(const std::vector<StepResult>& results, const std::string& csv_path) {
    double sum_oco_ovhd = 0, sum_st_ovhd = 0;
    double sum_oco_recv = 0, sum_st_recv = 0, sum_nf_recv = 0;
    double sum_oco_eff  = 0, sum_st_eff  = 0;
    // Phase-level accumulators
    double sum_oco_recv_p1 = 0, sum_oco_recv_p2 = 0, sum_oco_recv_p3 = 0;
    double sum_st_recv_p1  = 0, sum_st_recv_p2  = 0, sum_st_recv_p3  = 0;
    int    cnt_p1 = 0, cnt_p2 = 0, cnt_p3 = 0;

    for (const auto& r : results) {
        sum_oco_ovhd += r.oco_redundancy_pct;
        sum_st_ovhd  += r.static_redundancy_pct;
        sum_oco_recv += r.oco_recovery_pct;
        sum_st_recv  += r.static_recovery_pct;
        sum_nf_recv  += r.nofec_recovery_pct;
        sum_oco_eff  += r.oco_effective_rate;
        sum_st_eff   += r.static_effective_rate;
        if (r.phase == 1) { sum_oco_recv_p1 += r.oco_recovery_pct; sum_st_recv_p1 += r.static_recovery_pct; ++cnt_p1; }
        else if (r.phase == 2) { sum_oco_recv_p2 += r.oco_recovery_pct; sum_st_recv_p2 += r.static_recovery_pct; ++cnt_p2; }
        else { sum_oco_recv_p3 += r.oco_recovery_pct; sum_st_recv_p3 += r.static_recovery_pct; ++cnt_p3; }
    }
    int n = static_cast<int>(results.size());

    double avg_oco_ovhd = sum_oco_ovhd / n;
    double avg_st_ovhd  = sum_st_ovhd  / n;
    double avg_oco_recv = sum_oco_recv  / n;
    double avg_st_recv  = sum_st_recv   / n;
    double avg_nf_recv  = sum_nf_recv   / n;
    double avg_oco_eff  = sum_oco_eff   / n;
    double avg_st_eff   = sum_st_eff    / n;
    double saved_bw     = (avg_st_ovhd - avg_oco_ovhd);

    std::cout
        << "\n" << std::string(70, '=') << "\n"
        << "  仿真结果汇总\n"
        << std::string(70, '-') << "\n"
        << std::fixed << std::setprecision(2)
        << "  平均冗余开销（FEC 带宽占比）:\n"
        << "    OCO 自适应 :    " << avg_oco_ovhd << " %\n"
        << "    静态 FEC   :    " << avg_st_ovhd  << " %\n"
        << "    节省带宽   :    " << saved_bw      << " 百分点"
        << " （相对减少 " << saved_bw / avg_st_ovhd * 100.0 << "%）\n"
        << "\n"
        << "  平均有效编码率（k / (k+m)）:\n"
        << "    OCO 自适应 :    " << avg_oco_eff * 100.0 << " %\n"
        << "    静态 FEC   :    " << avg_st_eff  * 100.0 << " %  (固定 66.7%)\n"
        << "\n"
        << "  平均恢复成功率（全程）:\n"
        << "    OCO 自适应 :    " << avg_oco_recv << " %\n"
        << "    静态 FEC   :    " << avg_st_recv  << " %\n"
        << "    无 FEC     :    " << avg_nf_recv  << " %\n"
        << "\n"
        << "  按阶段恢复成功率:\n"
        << "    阶段1(良好) — OCO: " << (cnt_p1 ? sum_oco_recv_p1/cnt_p1 : 0.0)
        << "%  Static: "             << (cnt_p1 ? sum_st_recv_p1/cnt_p1  : 0.0) << "%\n"
        << "    阶段2(恶化) — OCO: " << (cnt_p2 ? sum_oco_recv_p2/cnt_p2 : 0.0)
        << "%  Static: "             << (cnt_p2 ? sum_st_recv_p2/cnt_p2  : 0.0) << "%\n"
        << "    阶段3(恢复) — OCO: " << (cnt_p3 ? sum_oco_recv_p3/cnt_p3 : 0.0)
        << "%  Static: "             << (cnt_p3 ? sum_st_recv_p3/cnt_p3  : 0.0) << "%\n"
        << std::string(70, '=') << "\n"
        << "\n"
        << "  ✓ 结果已保存：" << csv_path << "\n"
        << "  生成图表：python3 experiments/scripts/plot_oco_results.py\n\n";
}

// ── Main simulation ──────────────────────────────────────────────────────────

int main() {
    // Suppress INFO/DEBUG log output from internal OCO controller so the
    // progress table stays readable.
    Logger::instance().set_level(LogLevel::WARN);

    print_banner();

    // Fixed seeds: channel noise and Monte-Carlo FEC simulation are independent.
    std::mt19937 ch_rng(42);
    std::mt19937 sim_rng(100);

    // Instantiate OCO controller
    OCORedundancyController oco;
    oco.set_cost_weights(0.5, 0.3, 0.2);
    oco.set_redundancy_constraints(0.10, 1.0);  // 10%–100% redundancy range

    // Prepare output directory & CSV file
    std::string results_dir = "experiments/results";
    std::string csv_path    = results_dir + "/oco_simulation_results.csv";
    std::filesystem::create_directories(results_dir);
    std::ofstream csv(csv_path);
    if (!csv.is_open()) {
        std::cerr << "Error: cannot open " << csv_path << "\n"
                  << "  Make sure to run from the repository root directory.\n";
        return 1;
    }
    csv << "timestamp_s,phase,path0_rtt_ms,path1_rtt_ms,"
           "path0_loss_pct,path1_loss_pct,loss_correlation,"
           "oco_k,oco_m,oco_redundancy_pct,oco_effective_rate,oco_recovery_pct,"
           "static_k,static_m,static_redundancy_pct,static_effective_rate,static_recovery_pct,"
           "nofec_recovery_pct,best_path_loss_pct\n";

    std::vector<StepResult> results;
    results.reserve(SIM_STEPS);

    print_table_header();

    for (int t = 0; t < SIM_STEPS; ++t) {
        // 1. Get channel state
        ChannelState cs = get_channel(t, ch_rng);

        // 2. Feed both paths into OCO
        LinkMetrics m0;
        m0.path_id        = 0;
        m0.rtt_ms         = cs.path0_rtt;
        m0.loss_rate      = cs.path0_loss;
        m0.bandwidth_mbps = 150.0;
        m0.jitter_ms      = cs.path0_rtt * 0.10;
        oco.update_link_metrics(m0);

        LinkMetrics m1;
        m1.path_id        = 1;
        m1.rtt_ms         = cs.path1_rtt;
        m1.loss_rate      = cs.path1_loss;
        m1.bandwidth_mbps = 100.0;
        m1.jitter_ms      = cs.path1_rtt * 0.08;
        oco.update_link_metrics(m1);

        oco.update_loss_correlation(0, 1, cs.correlation);

        // 3. OCO decision
        RedundancyDecision decision = oco.compute_optimal_redundancy();

        // 4. The OCO picks the "best" (source) path; use that path's loss for
        //    a fair comparison — both OCO and static are assumed to route on
        //    the same best path.
        auto all_metrics = oco.get_all_metrics();
        double best_loss = cs.path0_loss;  // default
        bool source_found = false;
        for (const auto& pm : all_metrics) {
            if (pm.path_id == decision.source_path) {
                best_loss = pm.loss_rate;
                source_found = true;
                break;
            }
        }
        if (!source_found) {
            Logger::instance().log(LogLevel::WARN,
                "source_path ", decision.source_path,
                " not found in link_metrics; defaulting to path0 loss");
        }

        // 5. Monte-Carlo recovery simulation (same loss, different k/m)
        double oco_recv    = simulate_recovery(decision.k, decision.m,
                                               best_loss, N_GROUPS, sim_rng);
        double static_recv = simulate_recovery(static_cast<uint32_t>(STATIC_K),
                                               static_cast<uint32_t>(STATIC_M),
                                               best_loss, N_GROUPS, sim_rng);
        // No FEC: one packet per "group" — recovery only if that packet arrives
        std::bernoulli_distribution drop_nofec(best_loss);
        int nofec_ok = 0;
        for (int g = 0; g < N_GROUPS; ++g) {
            if (!drop_nofec(sim_rng)) ++nofec_ok;
        }
        double nofec_recv = static_cast<double>(nofec_ok) / N_GROUPS;

        // 6. Feedback so OCO can learn online
        double avg_rtt = (cs.path0_rtt + cs.path1_rtt) / 2.0;
        oco.feedback_update(best_loss, avg_rtt);

        // 7. Derived metrics
        double oco_eff_rate    = static_cast<double>(decision.k)
                                 / (decision.k + decision.m);
        double static_eff_rate = STATIC_K / (STATIC_K + STATIC_M);  // 4/6 ≈ 0.667

        // 8. Record
        StepResult r;
        r.step                  = t;
        r.phase                 = cs.phase;
        r.path0_rtt             = cs.path0_rtt;
        r.path1_rtt             = cs.path1_rtt;
        r.path0_loss_pct        = cs.path0_loss * 100.0;
        r.path1_loss_pct        = cs.path1_loss * 100.0;
        r.correlation           = cs.correlation;
        r.oco_k                 = decision.k;
        r.oco_m                 = decision.m;
        r.oco_redundancy_pct    = decision.redundancy_rate * 100.0;
        r.oco_effective_rate    = oco_eff_rate;
        r.oco_recovery_pct      = oco_recv    * 100.0;
        r.static_k              = static_cast<uint32_t>(STATIC_K);
        r.static_m              = static_cast<uint32_t>(STATIC_M);
        r.static_redundancy_pct = (STATIC_M / STATIC_K) * 100.0;
        r.static_effective_rate = static_eff_rate;
        r.static_recovery_pct  = static_recv  * 100.0;
        r.nofec_recovery_pct    = nofec_recv  * 100.0;
        r.best_path_loss_pct    = best_loss   * 100.0;
        results.push_back(r);

        // 9. CSV row
        csv << std::fixed << std::setprecision(3)
            << t                         << ","
            << cs.phase                  << ","
            << cs.path0_rtt              << ","
            << cs.path1_rtt              << ","
            << cs.path0_loss * 100.0     << ","
            << cs.path1_loss * 100.0     << ","
            << cs.correlation            << ","
            << decision.k                << ","
            << decision.m                << ","
            << decision.redundancy_rate * 100.0 << ","
            << oco_eff_rate              << ","
            << oco_recv * 100.0          << ","
            << static_cast<uint32_t>(STATIC_K) << ","
            << static_cast<uint32_t>(STATIC_M) << ","
            << (STATIC_M / STATIC_K) * 100.0 << ","
            << static_eff_rate           << ","
            << static_recv  * 100.0      << ","
            << nofec_recv   * 100.0      << ","
            << best_loss    * 100.0      << "\n";

        // 10. Print every 10th step (and first + last)
        if (t == 0 || t % 10 == 9 || t == SIM_STEPS - 1) {
            print_table_row(r);
        }
    }

    csv.close();
    print_summary(results, csv_path);
    return 0;
}
