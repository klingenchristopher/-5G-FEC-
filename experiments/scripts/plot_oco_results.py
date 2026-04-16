#!/usr/bin/env python3
"""
OCO 自适应 FEC 仿真结果可视化
============================

读取 demo_oco_adaptive 输出的 CSV，生成三张图（保存至 experiments/results/）：

  1. channel_dynamics.png  — 信道动态：RTT & 丢包率时间序列
  2. oco_adaptation.png    — OCO 自适应：k/m 参数 & 冗余率时变曲线
  3. performance_comparison.png — 性能对比：恢复率 & 有效编码率

运行方式（从仓库根目录）：
  python3 experiments/scripts/plot_oco_results.py
  # 或指定 CSV 路径：
  python3 experiments/scripts/plot_oco_results.py --csv path/to/results.csv
"""

import argparse
import csv
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')  # non-interactive backend (works without a display)
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np


# ── Data loading ─────────────────────────────────────────────────────────────

def load_csv(path: str) -> dict:
    """Load simulation CSV and return column arrays."""
    if not os.path.exists(path):
        print(f"Error: CSV file not found: {path}", file=sys.stderr)
        print("  Run ./build/bin/demo_oco_adaptive first.", file=sys.stderr)
        sys.exit(1)

    cols = {}
    with open(path, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            for k, v in row.items():
                cols.setdefault(k, []).append(float(v))

    return {k: np.array(v) for k, v in cols.items()}


# ── Helpers ──────────────────────────────────────────────────────────────────

PHASE_COLORS = {1: '#d4ecd4', 2: '#fde8d8', 3: '#d4e4f7'}
PHASE_LABELS = {1: '阶段1: 良好', 2: '阶段2: 恶化', 3: '阶段3: 恢复'}


def _shade_phases(ax, t, phase):
    """Draw translucent background rectangles for each phase."""
    changes = np.where(np.diff(phase) != 0)[0] + 1
    boundaries = np.concatenate([[0], changes, [len(phase)]])
    for i in range(len(boundaries) - 1):
        p = int(phase[boundaries[i]])
        ax.axvspan(t[boundaries[i]], t[boundaries[i+1] - 1],
                   color=PHASE_COLORS[p], alpha=0.45, lw=0)


def _add_phase_legend(ax):
    patches = [mpatches.Patch(color=PHASE_COLORS[p], alpha=0.6, label=PHASE_LABELS[p])
               for p in [1, 2, 3]]
    return patches


def _save(fig, path: str):
    fig.savefig(path, dpi=150, bbox_inches='tight')
    print(f"  保存: {path}")
    plt.close(fig)


# ── Figure 1: Channel dynamics ───────────────────────────────────────────────

def plot_channel_dynamics(d: dict, out_dir: str):
    t = d['timestamp_s']
    phase = d['phase']

    fig, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True)
    fig.suptitle('图1: 5G 信道动态（双路径）', fontsize=14, fontweight='bold', y=0.98)

    # — RTT ——————————————————————————————————————————————————————————————
    ax = axes[0]
    _shade_phases(ax, t, phase)
    ax.plot(t, d['path0_rtt_ms'], color='#2196F3', lw=1.4, label='路径0 (5G NR)')
    ax.plot(t, d['path1_rtt_ms'], color='#FF9800', lw=1.4, label='路径1 (Wi-Fi6)')
    ax.set_ylabel('RTT (ms)', fontsize=11)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)

    # — Loss rate ————————————————————————————————————————————————————————
    ax = axes[1]
    _shade_phases(ax, t, phase)
    ax.plot(t, d['path0_loss_pct'], color='#F44336', lw=1.4, label='路径0 丢包率')
    ax.plot(t, d['path1_loss_pct'], color='#9C27B0', lw=1.4, label='路径1 丢包率')
    ax.axhline(y=8.5, color='gray', ls='--', lw=0.8, label='k=4,m=2 保护上限(~8.5%)')
    ax.set_ylabel('丢包率 (%)', fontsize=11)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)

    # — Loss correlation ─────────────────────────────────────────────────
    ax = axes[2]
    _shade_phases(ax, t, phase)
    ax.plot(t, d['loss_correlation'], color='#607D8B', lw=1.4)
    ax.fill_between(t, 0, d['loss_correlation'], alpha=0.2, color='#607D8B')
    ax.set_xlabel('时间 (s)', fontsize=11)
    ax.set_ylabel('丢包相关性 ρ', fontsize=11)
    ax.set_ylim(-0.05, 0.6)
    ax.grid(True, alpha=0.3)

    # Phase legend
    handles = _add_phase_legend(axes[0])
    fig.legend(handles=handles, loc='upper center', ncol=3, fontsize=9,
               bbox_to_anchor=(0.5, 0.965))

    plt.tight_layout(rect=[0, 0, 1, 0.955])
    _save(fig, os.path.join(out_dir, 'channel_dynamics.png'))


# ── Figure 2: OCO adaptation ─────────────────────────────────────────────────

def plot_oco_adaptation(d: dict, out_dir: str):
    t = d['timestamp_s']
    phase = d['phase']

    # Actual redundancy ratio: m/k (discrete)
    actual_ovhd = d['oco_m'] / d['oco_k'] * 100.0

    fig, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True)
    fig.suptitle('图2: OCO 算法自适应过程', fontsize=14, fontweight='bold', y=0.98)

    # — k/m step plot ————————————————————————————————————————————————————
    ax = axes[0]
    _shade_phases(ax, t, phase)
    ax.step(t, d['oco_k'], where='post', color='#2196F3', lw=2.0, label='k (数据块数)')
    ax.step(t, d['oco_m'], where='post', color='#F44336', lw=2.0, label='m (冗余块数)')
    ax.set_ylabel('块数', fontsize=11)
    ax.set_yticks([1, 2, 3, 4, 5, 8, 10])
    ax.legend(fontsize=10, loc='upper right')
    ax.grid(True, alpha=0.3)

    # — Redundancy rate comparison ───────────────────────────────────────
    ax = axes[1]
    _shade_phases(ax, t, phase)
    ax.step(t, actual_ovhd, where='post',
            color='#4CAF50', lw=2.0, label='OCO 实际冗余率 (m/k × 100%)')
    ax.axhline(y=50.0, color='#FF5722', ls='--', lw=1.5, label='静态 FEC 冗余率 (50%)')
    ax.fill_between(t, actual_ovhd, 50.0,
                    where=(actual_ovhd < 50.0),
                    interpolate=True, alpha=0.15, color='green', label='节省带宽区域')
    ax.fill_between(t, actual_ovhd, 50.0,
                    where=(actual_ovhd > 50.0),
                    interpolate=True, alpha=0.15, color='red', label='额外开销区域')
    ax.set_ylabel('冗余率 (%)', fontsize=11)
    ax.set_ylim(-5, 120)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)

    # — Effective code rate ──────────────────────────────────────────────
    ax = axes[2]
    _shade_phases(ax, t, phase)
    oco_eff = d['oco_k'] / (d['oco_k'] + d['oco_m']) * 100.0
    ax.step(t, oco_eff, where='post',
            color='#009688', lw=2.0, label=f'OCO 有效编码率  均值 {oco_eff.mean():.1f}%')
    ax.axhline(y=100.0 * 4.0 / 6.0, color='#FF5722', ls='--', lw=1.5,
               label=f'静态 FEC 有效编码率  固定 {100*4/6:.1f}%')
    ax.set_xlabel('时间 (s)', fontsize=11)
    ax.set_ylabel('有效编码率 (%)', fontsize=11)
    ax.set_ylim(40, 105)
    ax.legend(fontsize=10, loc='lower right')
    ax.grid(True, alpha=0.3)

    handles = _add_phase_legend(axes[0])
    fig.legend(handles=handles, loc='upper center', ncol=3, fontsize=9,
               bbox_to_anchor=(0.5, 0.965))

    plt.tight_layout(rect=[0, 0, 1, 0.955])
    _save(fig, os.path.join(out_dir, 'oco_adaptation.png'))


# ── Figure 3: Performance comparison ────────────────────────────────────────

def plot_performance_comparison(d: dict, out_dir: str):
    t = d['timestamp_s']
    phase = d['phase']

    # Rolling 10-step average for smoother lines.
    # Note: np.convolve with mode='same' causes edge artifacts at the first and
    # last w/2 values (smaller effective window), which is acceptable here for
    # visualization purposes.
    def smooth(x, w=10):
        return np.convolve(x, np.ones(w)/w, mode='same')

    fig, axes = plt.subplots(2, 2, figsize=(14, 9))
    fig.suptitle('图3: 性能对比分析', fontsize=14, fontweight='bold', y=0.98)

    # ── Top-left: recovery rate over time ───────────────────────────────
    ax = axes[0, 0]
    _shade_phases(ax, t, phase)
    ax.plot(t, smooth(d['oco_recovery_pct']),    color='#4CAF50', lw=2.0, label='OCO 自适应')
    ax.plot(t, smooth(d['static_recovery_pct']), color='#2196F3', lw=2.0, ls='--', label='静态 FEC')
    ax.plot(t, smooth(d['nofec_recovery_pct']),  color='#F44336', lw=1.5, ls=':', label='无 FEC')
    ax.set_title('恢复成功率（10步滑动均值）', fontsize=11)
    ax.set_ylabel('恢复成功率 (%)', fontsize=10)
    ax.set_xlabel('时间 (s)', fontsize=10)
    ax.set_ylim(50, 105)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # ── Top-right: redundancy overhead over time ─────────────────────────
    ax = axes[0, 1]
    _shade_phases(ax, t, phase)
    actual_ovhd = d['oco_m'] / d['oco_k'] * 100.0
    ax.step(t, actual_ovhd, where='post', color='#4CAF50', lw=2.0, label='OCO 实际冗余率')
    ax.axhline(y=50.0, color='#2196F3', ls='--', lw=1.5, label='静态 FEC 冗余率 50%')
    ax.set_title('FEC 冗余率（带宽开销）', fontsize=11)
    ax.set_ylabel('冗余率 (%)', fontsize=10)
    ax.set_xlabel('时间 (s)', fontsize=10)
    ax.set_ylim(-5, 120)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # ── Bottom-left: per-phase bar chart ────────────────────────────────
    ax = axes[1, 0]
    phases = [1, 2, 3]
    phase_names = ['阶段1\n良好信道', '阶段2\n信道恶化', '阶段3\n信道恢复']
    oco_by_phase  = [d['oco_recovery_pct'][phase == p].mean() for p in phases]
    stat_by_phase = [d['static_recovery_pct'][phase == p].mean() for p in phases]
    nofec_by_phase= [d['nofec_recovery_pct'][phase == p].mean() for p in phases]

    x = np.arange(len(phases))
    w = 0.25
    ax.bar(x - w,   oco_by_phase,   w, color='#4CAF50', label='OCO 自适应', zorder=3)
    ax.bar(x,       stat_by_phase,  w, color='#2196F3', label='静态 FEC',   zorder=3)
    ax.bar(x + w,   nofec_by_phase, w, color='#F44336', label='无 FEC',     zorder=3)
    for xi, v in zip(x - w, oco_by_phase):
        ax.text(xi, v + 0.3, f'{v:.1f}%', ha='center', va='bottom', fontsize=7.5)
    for xi, v in zip(x, stat_by_phase):
        ax.text(xi, v + 0.3, f'{v:.1f}%', ha='center', va='bottom', fontsize=7.5)
    for xi, v in zip(x + w, nofec_by_phase):
        ax.text(xi, v + 0.3, f'{v:.1f}%', ha='center', va='bottom', fontsize=7.5)
    ax.set_xticks(x)
    ax.set_xticklabels(phase_names, fontsize=10)
    ax.set_ylabel('平均恢复成功率 (%)', fontsize=10)
    ax.set_title('各阶段平均恢复成功率', fontsize=11)
    ax.set_ylim(60, 107)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3, axis='y', zorder=0)

    # ── Bottom-right: overhead vs recovery scatter (Pareto frontier) ────
    ax = axes[1, 1]
    # Plot scatter: each step is one point
    sc = ax.scatter(actual_ovhd, d['oco_recovery_pct'],
                    c=t, cmap='plasma', s=12, alpha=0.6, label='OCO 各时步', zorder=3)
    # Static reference point
    ax.scatter([50.0], [d['static_recovery_pct'].mean()],
               marker='*', s=200, color='#2196F3', zorder=5, label=f'静态 FEC 均值')
    # OCO mean
    ax.scatter([actual_ovhd.mean()], [d['oco_recovery_pct'].mean()],
               marker='D', s=100, color='#4CAF50', zorder=5, label=f'OCO 均值')
    ax.annotate(f'Static\n({50.0:.0f}%, {d["static_recovery_pct"].mean():.1f}%)',
                xy=(50.0, d['static_recovery_pct'].mean()),
                xytext=(55, d['static_recovery_pct'].mean() - 4),
                fontsize=8, color='#2196F3',
                arrowprops=dict(arrowstyle='->', color='#2196F3', lw=0.8))
    ax.annotate(f'OCO\n({actual_ovhd.mean():.1f}%, {d["oco_recovery_pct"].mean():.1f}%)',
                xy=(actual_ovhd.mean(), d['oco_recovery_pct'].mean()),
                xytext=(actual_ovhd.mean() + 8, d['oco_recovery_pct'].mean() - 5),
                fontsize=8, color='#4CAF50',
                arrowprops=dict(arrowstyle='->', color='#4CAF50', lw=0.8))
    plt.colorbar(sc, ax=ax, label='时间步 (s)', shrink=0.8)
    ax.set_xlabel('FEC 冗余率 / 带宽开销 (%)', fontsize=10)
    ax.set_ylabel('恢复成功率 (%)', fontsize=10)
    ax.set_title('权衡曲线：开销 vs 恢复率', fontsize=11)
    ax.legend(fontsize=8, loc='lower right')
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    _save(fig, os.path.join(out_dir, 'performance_comparison.png'))


# ── Summary text ─────────────────────────────────────────────────────────────

def print_summary(d: dict):
    actual_ovhd = d['oco_m'] / d['oco_k'] * 100.0
    oco_eff = d['oco_k'] / (d['oco_k'] + d['oco_m']) * 100.0
    stat_eff = 100.0 * 4.0 / 6.0

    print("\n" + "=" * 62)
    print("  可视化数据汇总（用于进展汇报）")
    print("=" * 62)
    print(f"  平均冗余开销（实际 m/k × 100%）:")
    print(f"    OCO 自适应:  {actual_ovhd.mean():.1f} %")
    print(f"    静态 FEC  :  50.0 %")
    print(f"    节省带宽  :  {50.0 - actual_ovhd.mean():.1f} pp "
          f"（{(50.0 - actual_ovhd.mean())/50.0*100:.0f}% 相对减少）")
    print(f"\n  平均有效编码率:")
    print(f"    OCO 自适应:  {oco_eff.mean():.1f} %  (+{oco_eff.mean()-stat_eff:.1f} pp vs 静态)")
    print(f"    静态 FEC  :  {stat_eff:.1f} %（固定）")
    print(f"\n  平均恢复成功率:")
    print(f"    OCO 自适应:  {d['oco_recovery_pct'].mean():.2f} %")
    print(f"    静态 FEC  :  {d['static_recovery_pct'].mean():.2f} %")
    print(f"    无 FEC    :  {d['nofec_recovery_pct'].mean():.2f} %")
    print("=" * 62)


# ── Entry point ──────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='OCO 自适应FEC仿真结果可视化')
    parser.add_argument('--csv', default='experiments/results/oco_simulation_results.csv',
                        help='输入 CSV 文件路径')
    parser.add_argument('--output', default='experiments/results',
                        help='图表输出目录')
    args = parser.parse_args()

    out_dir = args.output
    Path(out_dir).mkdir(parents=True, exist_ok=True)

    print(f"\n  读取数据: {args.csv}")
    d = load_csv(args.csv)
    print(f"  加载完成：{len(d['timestamp_s'])} 个时间步")

    print("\n  生成图表...")
    plot_channel_dynamics(d, out_dir)
    plot_oco_adaptation(d, out_dir)
    plot_performance_comparison(d, out_dir)

    print_summary(d)
    print(f"\n  所有图表已保存至: {out_dir}/\n")


if __name__ == '__main__':
    main()
