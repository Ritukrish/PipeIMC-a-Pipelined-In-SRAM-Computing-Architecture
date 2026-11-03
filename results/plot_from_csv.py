#!/usr/bin/env python3
"""
PipeIMC Evaluation & Plotting Suite
Reads experimental CSV datasets and generates publication-quality IEEE figures:
  - Figure 1: Total Execution Cycles (from pipeimc_cycles_256.csv)
  - Figure 2: Normalized Speedup (from pipeimc_speedup.csv)
  - Figure 3: Per-Iteration Latency Scaling (from pipeimc_per_iteration_latency.csv)
  - Figure 4: Multi-Iteration Scalability Analysis (from pipeimc_scalability_sweep.csv)
  - Figure 5: Pipeline Microarchitecture Timeline / Gantt Chart (from pipeimc_matmul_timeline.csv)
"""

import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Apply publication-grade styling
plt.rcParams.update({
    'font.family': 'serif',
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 10,
    'figure.titlesize': 14,
    'figure.dpi': 300,
    'lines.linewidth': 1.8,
    'lines.markersize': 6
})

# ==============================================================================
# FIGURE 1: Total Execution Cycles Across Architectural Designs
# ==============================================================================
def plot_figure_1(csv_path='pipeimc_cycles_256.csv'):
    df = pd.read_csv(csv_path)
    
    workloads = [k.capitalize() for k in df['Kernel']]
    x = np.arange(len(workloads))
    width = 0.18

    fig, ax = plt.subplots(figsize=(8, 4.8))
    
    rects1 = ax.bar(x - 1.5*width, df['Design_1_Baseline'], width, label='Design 1: Baseline', color='#2b5c8f', edgecolor='black', linewidth=0.8)
    rects2 = ax.bar(x - 0.5*width, df['Design_2_Pipelined'], width, label='Design 2: Pipelined', color='#4682b4', edgecolor='black', linewidth=0.8)
    rects3 = ax.bar(x + 0.5*width, df['Design_3_Renaming'], width, label='Design 3: Renaming', color='#e08214', edgecolor='black', linewidth=0.8, hatch='//')
    rects4 = ax.bar(x + 1.5*width, df['Design_4_Fine_Grained'], width, label='Design 4: Fine-Grained', color='#2ca02c', edgecolor='black', linewidth=0.8)

    ax.set_ylabel('Total Execution Cycles (256 iterations)')
    ax.set_title('Execution Cycles Across PipeIMC Architectural Variants')
    ax.set_xticks(x)
    ax.set_xticklabels(workloads, fontweight='bold')
    ax.legend(frameon=True, facecolor='white', framealpha=0.9)
    ax.grid(axis='y', linestyle='--', alpha=0.5)
    ax.set_ylim(0, 100000)

    for rects in [rects1, rects2, rects3, rects4]:
        for rect in rects:
            h = rect.get_height()
            ax.annotate(f'{int(h):,}',
                        xy=(rect.get_x() + rect.get_width() / 2, h),
                        xytext=(0, 3), textcoords="offset points",
                        ha='center', va='bottom', fontsize=7.5, rotation=45)

    plt.tight_layout()
    plt.savefig('fig1_execution_cycles.png')
    plt.savefig('fig1_execution_cycles.pdf')
    plt.close()
    print("Generated: fig1_execution_cycles.png / .pdf")

# ==============================================================================
# FIGURE 2: Normalized Speedup Relative to In-Order Baseline
# ==============================================================================
def plot_figure_2(csv_path='pipeimc_speedup.csv'):
    df = pd.read_csv(csv_path)
    
    workloads = [k.capitalize() for k in df['Kernel']]
    x = np.arange(len(workloads))
    width = 0.18

    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    
    rects1 = ax.bar(x - 1.5*width, df['Design_1_Baseline'], width, label='Design 1: Baseline', color='#7f7f7f', edgecolor='black')
    rects2 = ax.bar(x - 0.5*width, df['Design_2_Pipelined'], width, label='Design 2: Pipelined', color='#1f77b4', edgecolor='black')
    rects3 = ax.bar(x + 0.5*width, df['Design_3_Renaming'], width, label='Design 3: Renaming', color='#ff7f0e', edgecolor='black', hatch='//')
    rects4 = ax.bar(x + 1.5*width, df['Design_4_Fine_Grained'], width, label='Design 4: Fine-Grained', color='#2ca02c', edgecolor='black')

    ax.set_ylabel('Speedup Relative to Baseline')
    ax.set_title('Normalized Performance Speedup Across Workloads')
    ax.set_xticks(x)
    ax.set_xticklabels(workloads, fontweight='bold')
    ax.axhline(1.0, color='red', linestyle='--', linewidth=1, alpha=0.7)
    ax.legend(frameon=True, loc='upper right')
    ax.grid(axis='y', linestyle='--', alpha=0.5)
    ax.set_ylim(0, 1.8)

    for rects in [rects1, rects2, rects3, rects4]:
        for rect in rects:
            h = rect.get_height()
            ax.annotate(f'{h:.2f}x',
                        xy=(rect.get_x() + rect.get_width() / 2, h),
                        xytext=(0, 3), textcoords="offset points",
                        ha='center', va='bottom', fontsize=8)

    plt.tight_layout()
    plt.savefig('fig2_normalized_speedup.png')
    plt.savefig('fig2_normalized_speedup.pdf')
    plt.close()
    print("Generated: fig2_normalized_speedup.png / .pdf")

# ==============================================================================
# FIGURE 3: Per-Iteration Latency Scaling Across Designs
# ==============================================================================
def plot_figure_3(csv_path='pipeimc_per_iteration_latency.csv'):
    df = pd.read_csv(csv_path)
    
    designs = [d.replace('Design ', 'D').split(':')[1].strip() for d in df['Design']]
    idx = np.arange(len(designs))
    w = 0.25

    fig, ax = plt.subplots(figsize=(8, 4.5))

    matmul_vals = df['Matmul_Cycles_Per_Iter']
    stencil_vals = df['Stencil_Cycles_Per_Iter']
    kmeans_vals = df['KMeans_Cycles_Per_Iter']

    ax.bar(idx - w, matmul_vals, w, label='Matmul', color='#1f77b4', edgecolor='black')
    ax.bar(idx, stencil_vals, w, label='Stencil', color='#ff7f0e', edgecolor='black')
    ax.bar(idx + w, kmeans_vals, w, label='K-Means', color='#2ca02c', edgecolor='black')

    ax.set_ylabel('Cycles per Loop Iteration')
    ax.set_title('Per-Iteration Latency Scaling Across Microarchitectural Designs')
    ax.set_xticks(idx)
    ax.set_xticklabels(designs, fontweight='bold')
    ax.legend(frameon=True)
    ax.grid(axis='y', linestyle='--', alpha=0.5)
    ax.set_ylim(0, 400)

    for i in range(len(designs)):
        ax.text(idx[i] - w, matmul_vals[i] + 5, f'{matmul_vals[i]}c', ha='center', fontsize=8.5)
        ax.text(idx[i], stencil_vals[i] + 5, f'{stencil_vals[i]}c', ha='center', fontsize=8.5)
        ax.text(idx[i] + w, kmeans_vals[i] + 5, f'{kmeans_vals[i]}c', ha='center', fontsize=8.5)

    plt.tight_layout()
    plt.savefig('fig3_iteration_latency.png')
    plt.savefig('fig3_iteration_latency.pdf')
    plt.close()
    print("Generated: fig3_iteration_latency.png / .pdf")

# ==============================================================================
# FIGURE 4: Scalability Analysis Across Iteration Counts
# ==============================================================================
def plot_figure_4(csv_path='pipeimc_scalability_sweep.csv'):
    df = pd.read_csv(csv_path)
    iters = df['Iterations']

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(14, 4.2), sharex=True)

    # Matmul Subplot
    ax1.plot(iters, df['Matmul_Baseline'], 'o--', color='#d62728', label='Baseline (341c/it)')
    ax1.plot(iters, df['Matmul_Fine_Grained'], 's-', color='#2ca02c', label='Fine-Grained (223c/it)')
    ax1.set_title('Matmul Scalability')
    ax1.set_xlabel('Iteration Count')
    ax1.set_ylabel('Total Cycles')
    ax1.grid(True, linestyle='--', alpha=0.5)
    ax1.legend(fontsize=8.5)

    # Stencil Subplot
    ax2.plot(iters, df['Stencil_Baseline'], 'o--', color='#d62728', label='Baseline (245c/it)')
    ax2.plot(iters, df['Stencil_Pipelined'], '^-', color='#1f77b4', label='Pipelined (223c/it)')
    ax2.set_title('Stencil Scalability')
    ax2.set_xlabel('Iteration Count')
    ax2.grid(True, linestyle='--', alpha=0.5)
    ax2.legend(fontsize=8.5)

    # K-Means Subplot
    ax3.plot(iters, df['KMeans_Baseline'], 'o--', color='#d62728', label='Baseline (204c/it)')
    ax3.plot(iters, df['KMeans_Pipelined'], 'd-', color='#9467bd', label='Pipelined (191c/it)')
    ax3.set_title('K-Means Scalability')
    ax3.set_xlabel('Iteration Count')
    ax3.grid(True, linestyle='--', alpha=0.5)
    ax3.legend(fontsize=8.5)

    plt.tight_layout()
    plt.savefig('fig4_scalability_analysis.png')
    plt.savefig('fig4_scalability_analysis.pdf')
    plt.close()
    print("Generated: fig4_scalability_analysis.png / .pdf")

# ==============================================================================
# FIGURE 5: Pipeline Microarchitecture Timeline / Gantt Chart
# ==============================================================================
def plot_figure_5(csv_path='pipeimc_matmul_timeline.csv'):
    df = pd.read_csv(csv_path)

    fig, (ax_base, ax_fine) = plt.subplots(2, 1, figsize=(12, 5.2), sharex=True)

    color_map = {
        'Memory': '#1f77b4',
        'Compute': '#aec7e8',
        'Memory_Overlapped': '#ff7f0e',
        'Memory_Compute': '#d62728',
        'Branch': '#9467bd'
    }

    # Plot Baseline Timeline
    df_base = df[df['Design'] == 'Baseline']
    for _, row in df_base.iterrows():
        c = color_map.get(row['Stage_Type'], '#c7c7c7')
        ax_base.broken_barh([(row['Start_Cycle'], row['Duration_Cycles'])], (10, 8),
                            facecolors=c, edgecolor='black', linewidth=0.7)
        if row['Duration_Cycles'] >= 20:
            ax_base.text(row['Start_Cycle'] + row['Duration_Cycles']/2, 14,
                         row['Operation_ID'].replace('_', ' '),
                         ha='center', va='center', fontsize=8, color='black', weight='bold')

    ax_base.set_ylim(5, 23)
    ax_base.set_yticks([])
    ax_base.set_ylabel('In-Order\nBaseline', fontweight='bold')
    ax_base.set_title('Timeline Comparison for Single Matmul Loop Iteration', fontsize=12)
    ax_base.grid(axis='x', linestyle=':', alpha=0.6)
    ax_base.axvline(341, color='red', linestyle='--', linewidth=1.2)
    ax_base.text(342, 20, 'End: 341c', color='red', fontsize=9, weight='bold')

    # Plot Fine-Grained Timeline
    df_fine = df[df['Design'] == 'Fine_Grained']
    for _, row in df_fine.iterrows():
        c = color_map.get(row['Stage_Type'], '#c7c7c7')
        ax_fine.broken_barh([(row['Start_Cycle'], row['Duration_Cycles'])], (10, 8),
                            facecolors=c, edgecolor='black', linewidth=0.7)
        if row['Duration_Cycles'] >= 25:
            ax_fine.text(row['Start_Cycle'] + row['Duration_Cycles']/2, 14,
                         row['Operation_ID'].replace('_', ' '),
                         ha='center', va='center', fontsize=8, color='black', weight='bold')

    ax_fine.set_ylim(5, 23)
    ax_fine.set_yticks([])
    ax_fine.set_ylabel('Fine-Grained\nIssue', fontweight='bold')
    ax_fine.set_xlabel('Clock Cycles')
    ax_fine.set_xlim(0, 360)
    ax_fine.grid(axis='x', linestyle=':', alpha=0.6)
    ax_fine.axvline(223, color='green', linestyle='--', linewidth=1.2)
    ax_fine.text(224, 20, 'End: 223c (-34.6%)', color='green', fontsize=9, weight='bold')

    plt.tight_layout()
    plt.savefig('fig5_pipeline_gantt.png')
    plt.savefig('fig5_pipeline_gantt.pdf')
    plt.close()
    print("Generated: fig5_pipeline_gantt.png / .pdf")

if __name__ == '__main__':
    plot_figure_1()
    plot_figure_2()
    plot_figure_3()
    plot_figure_4()
    plot_figure_5()
