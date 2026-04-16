#!/usr/bin/env python3
"""
5G链路质量分析脚本

用于分析5G链路trace文件，提取RTT、丢包率、带宽等指标，
并支持双路径丢包相关性分析（用于 OCO 冗余路径决策研究）。

新增功能（--dual-path）：
  - 计算两条路径丢包序列的滑动窗口 Pearson 相关系数
  - 绘制相关性时变曲线
  - 输出相关性统计（均值、方差、高相关区间占比）
"""

import argparse
import json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path


def analyze_trace(trace_file):
    """分析trace文件"""
    print(f"分析trace文件: {trace_file}")
    
    # 模拟数据（实际应从trace文件读取）
    timestamps = np.linspace(0, 100, 1000)
    rtt = 50 + 30 * np.sin(timestamps / 10) + np.random.normal(0, 5, 1000)
    loss_rate = 0.05 + 0.03 * np.sin(timestamps / 15) + np.random.normal(0, 0.01, 1000)
    bandwidth = 100 + 20 * np.sin(timestamps / 20) + np.random.normal(0, 5, 1000)
    
    # 确保合理范围
    rtt = np.clip(rtt, 10, 200)
    loss_rate = np.clip(loss_rate, 0, 0.3)
    bandwidth = np.clip(bandwidth, 20, 150)
    
    return {
        'timestamps': timestamps,
        'rtt': rtt,
        'loss_rate': loss_rate,
        'bandwidth': bandwidth
    }


# ── Loss correlation analysis (dual-path) ────────────────────────────────────

def compute_sliding_correlation(series1: np.ndarray, series2: np.ndarray,
                                 window: int = 30) -> np.ndarray:
    """
    计算两个序列的滑动窗口 Pearson 相关系数。

    对每个位置 i，使用 [i-window//2, i+window//2] 区间内的样本计算
    Pearson 相关系数 ρ ∈ [-1, 1]。边界处使用可用样本（不填充）。

    参数
    ----
    series1, series2 : np.ndarray
        等长的一维时间序列（例如两条路径的丢包率序列）
    window : int
        滑动窗口宽度（样本数），默认 30

    返回
    ----
    np.ndarray
        每步的相关系数，与输入等长
    """
    n = len(series1)
    half = window // 2
    corr = np.full(n, np.nan)
    for i in range(n):
        lo = max(0, i - half)
        hi = min(n, i + half + 1)
        s1 = series1[lo:hi]
        s2 = series2[lo:hi]
        if len(s1) < 3:
            # Minimum of 3 samples required for a meaningful Pearson correlation
            # (fewer samples yield undefined or trivially perfect correlations).
            corr[i] = 0.0
            continue
        std1, std2 = np.std(s1), np.std(s2)
        if std1 < 1e-12 or std2 < 1e-12:
            corr[i] = 0.0
        else:
            corr[i] = np.corrcoef(s1, s2)[0, 1]
    return np.nan_to_num(corr, nan=0.0)


def generate_dual_path_trace(n: int = 200, seed: int = 42) -> dict:
    """
    生成双路径仿真 trace（用于演示，实际应从真实 trace 读取）。

    三阶段模型与 demo_oco_adaptive 保持一致：
      Phase 1 (0–59)   : 良好信道，低相关
      Phase 2 (60–139) : 信道恶化，高相关（联合干扰）
      Phase 3 (140–199): 恢复至良好水平
    """
    rng = np.random.default_rng(seed)
    t = np.arange(n)

    rtt0 = np.where(t < 60,  25 + 5*np.sin(2*np.pi*t/30),
           np.where(t < 140, 80 + 10*np.sin(2*np.pi*t/12),
                              25 + 5*np.sin(2*np.pi*t/30))) + rng.normal(0, 3, n)
    rtt1 = np.where(t < 60,  40 + 8*np.sin(2*np.pi*t/25),
           np.where(t < 140, 65 + 8*np.sin(2*np.pi*t/15),
                              40 + 7*np.sin(2*np.pi*t/25))) + rng.normal(0, 3, n)

    loss0_base = np.where(t < 60,  0.030 + 0.008*np.sin(2*np.pi*t/20),
                 np.where(t < 80,  np.interp(t, [60, 80], [0.030, 0.220]),
                 np.where(t < 140, 0.200 + 0.055*np.sin(2*np.pi*t/8),
                 np.where(t < 165, np.interp(t, [140, 165], [0.200, 0.030]),
                                   0.030 + 0.008*np.sin(2*np.pi*t/28)))))
    loss1_base = np.where(t < 60,  0.012 + 0.005*np.sin(2*np.pi*t/15),
                 np.where(t < 80,  np.interp(t, [60, 80], [0.012, 0.120]),
                 np.where(t < 140, 0.110 + 0.040*np.sin(2*np.pi*t/10),
                 np.where(t < 165, np.interp(t, [140, 165], [0.110, 0.012]),
                                   0.012 + 0.004*np.sin(2*np.pi*t/22)))))

    # Add correlated noise component (high correlation in Phase 2)
    common_noise = rng.normal(0, 0.015, n)
    corr_strength = np.where(t < 60, 0.05, np.where(t < 140, 0.40, 0.05))
    loss0 = np.clip(loss0_base + corr_strength * common_noise + rng.normal(0, 0.005, n), 0.001, 0.5)
    loss1 = np.clip(loss1_base + corr_strength * common_noise + rng.normal(0, 0.004, n), 0.001, 0.5)

    phase = np.where(t < 60, 1, np.where(t < 140, 2, 3))

    return {
        'timestamps': t,
        'rtt0': np.clip(rtt0, 5, 200),
        'rtt1': np.clip(rtt1, 5, 200),
        'loss0': loss0,
        'loss1': loss1,
        'phase': phase,
    }


def analyze_loss_correlation(data: dict, window: int = 20) -> dict:
    """
    计算双路径丢包相关性统计。

    参数
    ----
    data   : 包含 'loss0', 'loss1', 'timestamps', 'phase' 的字典
    window : 滑动窗口宽度

    返回
    ----
    dict，包含 'corr_ts'（相关系数时序）及各阶段统计
    """
    corr_ts = compute_sliding_correlation(data['loss0'], data['loss1'], window=window)
    phase = data['phase']

    HIGH_CORR_THRESHOLD = 0.3  # ρ > 0.3 视为高相关

    result = {
        'corr_ts': corr_ts,
        'overall': {
            'mean': float(np.mean(corr_ts)),
            'std':  float(np.std(corr_ts)),
            'max':  float(np.max(corr_ts)),
            'high_corr_ratio': float(np.mean(corr_ts > HIGH_CORR_THRESHOLD)),
        },
    }
    for p, name in [(1, 'phase1_good'), (2, 'phase2_degraded'), (3, 'phase3_recovery')]:
        mask = (phase == p)
        if mask.sum() == 0:
            continue
        c = corr_ts[mask]
        result[name] = {
            'mean': float(np.mean(c)),
            'std':  float(np.std(c)),
            'high_corr_ratio': float(np.mean(c > HIGH_CORR_THRESHOLD)),
        }

    return result


def plot_loss_correlation(data: dict, corr_result: dict, output_dir: str,
                           window: int = 20):
    """
    绘制双路径丢包相关性分析图（三子图）并保存。

    子图1: 两路径丢包率时序
    子图2: 滑动窗口相关系数 ρ(t)
    子图3: 相关系数分布直方图（按阶段）
    """
    t = data['timestamps']
    phase = data['phase']
    corr_ts = corr_result['corr_ts']

    PHASE_COLORS = {1: '#d4ecd4', 2: '#fde8d8', 3: '#d4e4f7'}
    HIGH_CORR_THRESHOLD = 0.3

    def shade(ax):
        changes = np.concatenate([[0], np.where(np.diff(phase) != 0)[0] + 1, [len(phase)]])
        for i in range(len(changes) - 1):
            p = int(phase[changes[i]])
            ax.axvspan(t[changes[i]], t[changes[i+1] - 1],
                       color=PHASE_COLORS[p], alpha=0.4, lw=0)

    fig, axes = plt.subplots(3, 1, figsize=(12, 10))
    fig.suptitle('双路径丢包相关性分析', fontsize=14, fontweight='bold', y=0.98)

    # — Per-path loss rate ——————————————————————————————————————————————
    ax = axes[0]
    shade(ax)
    ax.plot(t, data['loss0'] * 100, color='#F44336', lw=1.4, label='路径0 丢包率')
    ax.plot(t, data['loss1'] * 100, color='#9C27B0', lw=1.4, label='路径1 丢包率')
    ax.set_ylabel('丢包率 (%)', fontsize=11)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)

    # — Sliding correlation ——————————————————————————————————————————————
    ax = axes[1]
    shade(ax)
    ax.plot(t, corr_ts, color='#1565C0', lw=1.8, label=f'ρ(t)（窗口 {window} 步）')
    ax.fill_between(t, corr_ts, 0, where=(corr_ts > HIGH_CORR_THRESHOLD),
                    alpha=0.25, color='red', label=f'高相关区间（ρ > {HIGH_CORR_THRESHOLD}）')
    ax.axhline(y=HIGH_CORR_THRESHOLD, color='red', ls='--', lw=0.9, alpha=0.7)
    ax.axhline(y=0, color='gray', ls='-', lw=0.5)
    ax.set_ylabel('Pearson ρ', fontsize=11)
    ax.set_ylim(-0.3, 0.9)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # — Histogram by phase ——————————————————————————————————————————————
    ax = axes[2]
    colors = ['#4CAF50', '#FF9800', '#2196F3']
    labels = ['阶段1: 良好', '阶段2: 恶化', '阶段3: 恢复']
    for p, color, label in zip([1, 2, 3], colors, labels):
        mask = (phase == p)
        if mask.sum() == 0:
            continue
        ax.hist(corr_ts[mask], bins=25, alpha=0.55, color=color, label=label, density=True)
    ax.axvline(x=HIGH_CORR_THRESHOLD, color='red', ls='--', lw=1.0,
               label=f'高相关阈值 {HIGH_CORR_THRESHOLD}')
    ax.set_xlabel('Pearson 相关系数 ρ', fontsize=11)
    ax.set_ylabel('概率密度', fontsize=11)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    out_path = Path(output_dir) / 'loss_correlation.png'
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"  丢包相关性图表已保存: {out_path}")
    plt.close()


def print_correlation_stats(corr_result: dict):
    """打印相关性统计摘要（适合进展汇报使用）。"""
    print("\n  --- 丢包相关性分析结果 ---")
    ov = corr_result['overall']
    print(f"  全程均值 ρ̄ = {ov['mean']:.3f}  ±{ov['std']:.3f}  "
          f"最大值 {ov['max']:.3f}")
    print(f"  高相关区间占比（ρ > 0.3）: {ov['high_corr_ratio']*100:.1f} %")
    print()
    for key, name in [('phase1_good', '阶段1 良好'),
                      ('phase2_degraded', '阶段2 恶化'),
                      ('phase3_recovery', '阶段3 恢复')]:
        if key not in corr_result:
            continue
        s = corr_result[key]
        print(f"  {name}: ρ̄={s['mean']:.3f} ±{s['std']:.3f}  "
              f"高相关占比 {s['high_corr_ratio']*100:.1f}%")
    print()
    print("  解读：")
    if corr_result.get('phase2_degraded', {}).get('mean', 0) > \
       corr_result.get('phase1_good', {}).get('mean', 0) * 2:
        print("  ✓ 信道恶化阶段丢包相关性显著升高 → OCO 需将冗余包路由至低相关路径")
    else:
        print("  → 两路径丢包基本独立，跨路径冗余分配效果最优")


def plot_metrics(data, output_dir):
    """绘制指标图表"""
    fig, axes = plt.subplots(3, 1, figsize=(12, 10))
    
    # RTT
    axes[0].plot(data['timestamps'], data['rtt'], 'b-', linewidth=1)
    axes[0].set_ylabel('RTT (ms)', fontsize=12)
    axes[0].set_title('5G链路质量指标', fontsize=14, fontweight='bold')
    axes[0].grid(True, alpha=0.3)
    
    # 丢包率
    axes[1].plot(data['timestamps'], data['loss_rate'] * 100, 'r-', linewidth=1)
    axes[1].set_ylabel('丢包率 (%)', fontsize=12)
    axes[1].grid(True, alpha=0.3)
    
    # 带宽
    axes[2].plot(data['timestamps'], data['bandwidth'], 'g-', linewidth=1)
    axes[2].set_xlabel('时间 (s)', fontsize=12)
    axes[2].set_ylabel('带宽 (Mbps)', fontsize=12)
    axes[2].grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    output_file = Path(output_dir) / 'metrics.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"图表已保存: {output_file}")
    
    plt.close()


def generate_statistics(data):
    """生成统计信息"""
    stats = {
        'rtt': {
            'mean': float(np.mean(data['rtt'])),
            'std': float(np.std(data['rtt'])),
            'min': float(np.min(data['rtt'])),
            'max': float(np.max(data['rtt'])),
            'p50': float(np.percentile(data['rtt'], 50)),
            'p95': float(np.percentile(data['rtt'], 95)),
            'p99': float(np.percentile(data['rtt'], 99)),
        },
        'loss_rate': {
            'mean': float(np.mean(data['loss_rate'])),
            'std': float(np.std(data['loss_rate'])),
            'min': float(np.min(data['loss_rate'])),
            'max': float(np.max(data['loss_rate'])),
        },
        'bandwidth': {
            'mean': float(np.mean(data['bandwidth'])),
            'std': float(np.std(data['bandwidth'])),
            'min': float(np.min(data['bandwidth'])),
            'max': float(np.max(data['bandwidth'])),
        }
    }
    
    return stats


def main():
    parser = argparse.ArgumentParser(description='5G链路质量分析工具')
    parser.add_argument('--trace', type=str, default='../traces/5g_maritime.trace',
                       help='Trace文件路径')
    parser.add_argument('--output', type=str, default='../traces',
                       help='输出目录')
    parser.add_argument('--dual-path', action='store_true',
                       help='启用双路径丢包相关性分析')
    parser.add_argument('--corr-window', type=int, default=20,
                       help='相关性计算的滑动窗口宽度（默认20步）')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("5G链路质量分析工具")
    print("=" * 60)
    
    # 分析trace
    data = analyze_trace(args.trace)
    
    # 生成统计
    stats = generate_statistics(data)
    
    print("\n统计信息:")
    print(f"  RTT: {stats['rtt']['mean']:.2f} ± {stats['rtt']['std']:.2f} ms")
    print(f"       [P50: {stats['rtt']['p50']:.2f}, P95: {stats['rtt']['p95']:.2f}, P99: {stats['rtt']['p99']:.2f}]")
    print(f"  丢包率: {stats['loss_rate']['mean']*100:.2f} ± {stats['loss_rate']['std']*100:.2f} %")
    print(f"  带宽: {stats['bandwidth']['mean']:.2f} ± {stats['bandwidth']['std']:.2f} Mbps")
    
    # 保存统计信息
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    stats_file = output_dir / 'statistics.json'
    with open(stats_file, 'w', encoding='utf-8') as f:
        json.dump(stats, f, indent=2, ensure_ascii=False)
    print(f"\n统计数据已保存: {stats_file}")
    
    # 绘制图表
    plot_metrics(data, output_dir)

    # 双路径丢包相关性分析
    if args.dual_path:
        print("\n" + "=" * 60)
        print("双路径丢包相关性分析")
        print("=" * 60)
        dual_data = generate_dual_path_trace(n=200, seed=42)
        corr_result = analyze_loss_correlation(dual_data, window=args.corr_window)
        print_correlation_stats(corr_result)
        plot_loss_correlation(dual_data, corr_result, str(output_dir),
                              window=args.corr_window)
        # Persist correlation stats
        corr_file = output_dir / 'loss_correlation_stats.json'
        serializable = {
            k: (v.tolist() if isinstance(v, np.ndarray) else v)
            for k, v in corr_result.items()
        }
        with open(corr_file, 'w', encoding='utf-8') as f:
            json.dump(serializable, f, indent=2, ensure_ascii=False)
        print(f"  相关性统计已保存: {corr_file}")
    
    print("\n分析完成！")
    print("=" * 60)


if __name__ == '__main__':
    main()
