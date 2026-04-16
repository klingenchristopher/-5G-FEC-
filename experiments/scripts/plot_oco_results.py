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
PHASE_LABELS = {1: 'Phase 1: Good', 2: 'Phase 2: Degraded', 3: 'Phase 3: Recovery'}


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
    fig.savefig(path, dpi=220, facecolor='white')
    print(f"  保存: {path}")
    plt.close(fig)


# ── Figure 1: Channel dynamics ───────────────────────────────────────────────

def plot_channel_dynamics(d: dict, out_dir: str):
    t = d['timestamp_s']
    phase = d['phase']

    fig, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True, constrained_layout=True)
    fig.suptitle('Figure 1: 5G Channel Dynamics (Dual Path)', fontsize=14, fontweight='bold')

    # — RTT ——————————————————————————————————————————————————————————————
    ax = axes[0]
    _shade_phases(ax, t, phase)
    ax.plot(t, d['path0_rtt_ms'], color='#2196F3', lw=1.4, label='Path 0 (5G NR)')
    ax.plot(t, d['path1_rtt_ms'], color='#FF9800', lw=1.4, label='Path 1 (Wi-Fi6)')
    ax.set_ylabel('RTT (ms)', fontsize=11)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)

    # — Loss rate ————————————————————————————————————————————————————————
    ax = axes[1]
    _shade_phases(ax, t, phase)
    ax.plot(t, d['path0_loss_pct'], color='#F44336', lw=1.4, label='Path 0 Loss')
    ax.plot(t, d['path1_loss_pct'], color='#9C27B0', lw=1.4, label='Path 1 Loss')
    ax.axhline(y=8.5, color='gray', ls='--', lw=0.8, label='k=4,m=2 protection limit (~8.5%)')
    ax.set_ylabel('Loss Rate (%)', fontsize=11)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)

    # — Loss correlation ─────────────────────────────────────────────────
    ax = axes[2]
    _shade_phases(ax, t, phase)
    ax.plot(t, d['loss_correlation'], color='#607D8B', lw=1.4)
    ax.fill_between(t, 0, d['loss_correlation'], alpha=0.2, color='#607D8B')
    ax.set_xlabel('Time (s)', fontsize=11)
    ax.set_ylabel('Loss Correlation ρ', fontsize=11)
    ax.set_ylim(-0.05, 0.6)
    ax.grid(True, alpha=0.3)

    # Phase legend
    handles = _add_phase_legend(axes[0])
    fig.legend(handles=handles, loc='upper center', ncol=3, fontsize=9,
               bbox_to_anchor=(0.5, 0.965))

    _save(fig, os.path.join(out_dir, 'channel_dynamics.png'))


# ── Figure 2: OCO adaptation ─────────────────────────────────────────────────

def plot_oco_adaptation(d: dict, out_dir: str):
    t = d['timestamp_s']
    phase = d['phase']

    # Actual redundancy ratio: m/k (discrete)
    actual_ovhd = d['oco_m'] / d['oco_k'] * 100.0

    fig, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True, constrained_layout=True)
    fig.suptitle('Figure 2: OCO Adaptation Process', fontsize=14, fontweight='bold')

    # — k/m step plot ————————————————————————————————————————————————————
    ax = axes[0]
    _shade_phases(ax, t, phase)
    ax.step(t, d['oco_k'], where='post', color='#2196F3', lw=2.0, label='k (source blocks)')
    ax.step(t, d['oco_m'], where='post', color='#F44336', lw=2.0, label='m (repair blocks)')
    ax.set_ylabel('Block Count', fontsize=11)
    ax.set_yticks([1, 2, 3, 4, 5, 8, 10])
    ax.legend(fontsize=10, loc='upper right')
    ax.grid(True, alpha=0.3)

    # — Redundancy rate comparison ───────────────────────────────────────
    ax = axes[1]
    _shade_phases(ax, t, phase)
    ax.step(t, actual_ovhd, where='post',
            color='#4CAF50', lw=2.0, label='OCO actual overhead (m/k × 100%)')
    ax.axhline(y=50.0, color='#FF5722', ls='--', lw=1.5, label='Static FEC overhead (50%)')
    ax.fill_between(t, actual_ovhd, 50.0,
                    where=(actual_ovhd < 50.0),
                    interpolate=True, alpha=0.15, color='green', label='Bandwidth saving area')
    ax.fill_between(t, actual_ovhd, 50.0,
                    where=(actual_ovhd > 50.0),
                    interpolate=True, alpha=0.15, color='red', label='Extra overhead area')
    ax.set_ylabel('Overhead (%)', fontsize=11)
    ax.set_ylim(-5, 120)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)

    # — Effective code rate ──────────────────────────────────────────────
    ax = axes[2]
    _shade_phases(ax, t, phase)
    oco_eff = d['oco_k'] / (d['oco_k'] + d['oco_m']) * 100.0
    ax.step(t, oco_eff, where='post',
            color='#009688', lw=2.0, label=f'OCO effective code rate  mean {oco_eff.mean():.1f}%')
    ax.axhline(y=100.0 * 4.0 / 6.0, color='#FF5722', ls='--', lw=1.5,
               label=f'Static FEC effective code rate  fixed {100*4/6:.1f}%')
    ax.set_xlabel('Time (s)', fontsize=11)
    ax.set_ylabel('Effective Code Rate (%)', fontsize=11)
    ax.set_ylim(40, 105)
    ax.legend(fontsize=10, loc='lower right')
    ax.grid(True, alpha=0.3)

    handles = _add_phase_legend(axes[0])
    fig.legend(handles=handles, loc='upper center', ncol=3, fontsize=9,
               bbox_to_anchor=(0.5, 0.965))

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

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), constrained_layout=True)
    fig.suptitle('Figure 3: Performance Comparison', fontsize=14, fontweight='bold')

    # ── Top-left: recovery rate over time ───────────────────────────────
    ax = axes[0, 0]
    _shade_phases(ax, t, phase)
    ax.plot(t, smooth(d['oco_recovery_pct']),    color='#4CAF50', lw=2.0, label='OCO adaptive')
    ax.plot(t, smooth(d['static_recovery_pct']), color='#2196F3', lw=2.0, ls='--', label='Static FEC')
    ax.plot(t, smooth(d['nofec_recovery_pct']),  color='#F44336', lw=1.5, ls=':', label='No FEC')
    ax.set_title('Recovery Success Rate (10-step rolling mean)', fontsize=11)
    ax.set_ylabel('Recovery Success Rate (%)', fontsize=10)
    ax.set_xlabel('Time (s)', fontsize=10)
    ax.set_ylim(50, 105)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # ── Top-right: redundancy overhead over time ─────────────────────────
    ax = axes[0, 1]
    _shade_phases(ax, t, phase)
    actual_ovhd = d['oco_m'] / d['oco_k'] * 100.0
    ax.step(t, actual_ovhd, where='post', color='#4CAF50', lw=2.0, label='OCO actual overhead')
    ax.axhline(y=50.0, color='#2196F3', ls='--', lw=1.5, label='Static FEC overhead 50%')
    ax.set_title('FEC Overhead (Bandwidth Cost)', fontsize=11)
    ax.set_ylabel('Overhead (%)', fontsize=10)
    ax.set_xlabel('Time (s)', fontsize=10)
    ax.set_ylim(-5, 120)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # ── Bottom-left: per-phase bar chart ────────────────────────────────
    ax = axes[1, 0]
    phases = [1, 2, 3]
    phase_names = ['Phase 1\nGood', 'Phase 2\nDegraded', 'Phase 3\nRecovery']
    oco_by_phase  = [d['oco_recovery_pct'][phase == p].mean() for p in phases]
    stat_by_phase = [d['static_recovery_pct'][phase == p].mean() for p in phases]
    nofec_by_phase= [d['nofec_recovery_pct'][phase == p].mean() for p in phases]

    x = np.arange(len(phases))
    w = 0.25
    ax.bar(x - w,   oco_by_phase,   w, color='#4CAF50', label='OCO adaptive', zorder=3)
    ax.bar(x,       stat_by_phase,  w, color='#2196F3', label='Static FEC',   zorder=3)
    ax.bar(x + w,   nofec_by_phase, w, color='#F44336', label='No FEC',       zorder=3)
    for xi, v in zip(x - w, oco_by_phase):
        ax.text(xi, v + 0.3, f'{v:.1f}%', ha='center', va='bottom', fontsize=7.5)
    for xi, v in zip(x, stat_by_phase):
        ax.text(xi, v + 0.3, f'{v:.1f}%', ha='center', va='bottom', fontsize=7.5)
    for xi, v in zip(x + w, nofec_by_phase):
        ax.text(xi, v + 0.3, f'{v:.1f}%', ha='center', va='bottom', fontsize=7.5)
    ax.set_xticks(x)
    ax.set_xticklabels(phase_names, fontsize=10)
    ax.set_ylabel('Average Recovery Success Rate (%)', fontsize=10)
    ax.set_title('Average Recovery Rate by Phase', fontsize=11)
    ax.set_ylim(60, 107)
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3, axis='y', zorder=0)

    # ── Bottom-right: overhead vs recovery scatter (Pareto frontier) ────
    ax = axes[1, 1]
    # Plot scatter: each step is one point
    sc = ax.scatter(actual_ovhd, d['oco_recovery_pct'],
                    c=t, cmap='plasma', s=12, alpha=0.6, label='OCO steps', zorder=3)
    # Static reference point
    ax.scatter([50.0], [d['static_recovery_pct'].mean()],
               marker='*', s=200, color='#2196F3', zorder=5, label='Static FEC mean')
    # OCO mean
    ax.scatter([actual_ovhd.mean()], [d['oco_recovery_pct'].mean()],
               marker='D', s=100, color='#4CAF50', zorder=5, label='OCO mean')
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
    plt.colorbar(sc, ax=ax, label='Time step (s)', shrink=0.8)
    ax.set_xlabel('FEC Overhead / Bandwidth Cost (%)', fontsize=10)
    ax.set_ylabel('Recovery Success Rate (%)', fontsize=10)
    ax.set_title('Tradeoff Curve: Overhead vs Recovery', fontsize=11)
    ax.legend(fontsize=8, loc='lower right')
    ax.grid(True, alpha=0.3)

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
