import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# 1. GENERATE CSVs
df_cycles = pd.DataFrame({
    'Kernel': ['matmul', 'stencil', 'kmeans'],
    'Iterations': [256, 256, 256],
    'Total_Operations': [1280, 1280, 1024],
    'Design_1_Baseline': [87297, 62721, 52225],
    'Design_2_Pipelined': [73729, 57089, 48897],
    'Design_3_Renaming': [73729, 57089, 48897],
    'Design_4_Fine_Grained': [57089, 57089, 48897]
})
df_cycles.to_csv('pipeimc_cycles_256.csv', index=False)
print("Saved: pipeimc_cycles_256.csv")

df_speedup = pd.DataFrame({
    'Kernel': ['matmul', 'stencil', 'kmeans'],
    'Design_1_Baseline': [1.000, 1.000, 1.000],
    'Design_2_Pipelined': [round(87297/73729, 4), round(62721/57089, 4), round(52225/48897, 4)],
    'Design_3_Renaming': [round(87297/73729, 4), round(62721/57089, 4), round(52225/48897, 4)],
    'Design_4_Fine_Grained': [round(87297/57089, 4), round(62721/57089, 4), round(52225/48897, 4)]
})
df_speedup.to_csv('pipeimc_speedup.csv', index=False)
print("Saved: pipeimc_speedup.csv")

df_latency = pd.DataFrame({
    'Design': ['Design 1: Baseline', 'Design 2: Pipelined', 'Design 3: Renaming', 'Design 4: Fine-Grained'],
    'Matmul_Cycles_Per_Iter': [341, 288, 288, 223],
    'Stencil_Cycles_Per_Iter': [245, 223, 223, 223],
    'KMeans_Cycles_Per_Iter': [204, 191, 191, 191]
})
df_latency.to_csv('pipeimc_per_iteration_latency.csv', index=False)
print("Saved: pipeimc_per_iteration_latency.csv")

iterations = [32, 64, 128, 256, 512]
scaling_data = []
for it in iterations:
    scaling_data.append({
        'Iterations': it,
        'Matmul_Baseline': it * 341 + 1,
        'Matmul_Pipelined': it * 288 + 1,
        'Matmul_Fine_Grained': it * 223 + 1,
        'Stencil_Baseline': it * 245 + 1,
        'Stencil_Pipelined': it * 223 + 1,
        'KMeans_Baseline': it * 204 + 1,
        'KMeans_Pipelined': it * 191 + 1
    })
df_scaling = pd.DataFrame(scaling_data)
df_scaling.to_csv('pipeimc_scalability_sweep.csv', index=False)
print("Saved: pipeimc_scalability_sweep.csv")

df_timeline = pd.DataFrame({
    'Design': [
        'Baseline', 'Baseline', 'Baseline', 'Baseline', 'Baseline', 'Baseline', 'Baseline',
        'Fine_Grained', 'Fine_Grained', 'Fine_Grained', 'Fine_Grained', 'Fine_Grained', 'Fine_Grained', 'Fine_Grained'
    ],
    'Operation_ID': [
        'O1_Loads', 'O1_MUL_Calc', 'O2_Loads', 'O2_MUL_Calc', 'O3_ADD', 'O4_ADD_Store', 'B_Branch',
        'O1_Loads', 'O1_MUL_Calc', 'O2_Loads_Overlapped', 'O2_MUL_Calc', 'O3_ADD', 'O4_ADD_Store', 'B_Branch'
    ],
    'Start_Cycle': [
        0, 40, 145, 185, 290, 299, 328,
        0, 40, 40, 145, 190, 199, 210
    ],
    'Duration_Cycles': [
        40, 105, 40, 105, 9, 29, 13,
        40, 105, 40, 45, 9, 11, 13
    ],
    'End_Cycle': [
        40, 145, 185, 290, 299, 328, 341,
        40, 145, 80, 190, 199, 210, 223
    ],
    'Stage_Type': [
        'Memory', 'Compute', 'Memory', 'Compute', 'Compute', 'Memory_Compute', 'Branch',
        'Memory', 'Compute', 'Memory_Overlapped', 'Compute', 'Compute', 'Memory_Compute', 'Branch'
    ]
})
df_timeline.to_csv('pipeimc_matmul_timeline.csv', index=False)
print("Saved: pipeimc_matmul_timeline.csv")

# 2. PLOT FIGURES
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

# Fig 1
df1 = pd.read_csv('pipeimc_cycles_256.csv')
workloads = [k.capitalize() for k in df1['Kernel']]
x = np.arange(len(workloads))
width = 0.18

fig, ax = plt.subplots(figsize=(8, 4.8))
rects1 = ax.bar(x - 1.5*width, df1['Design_1_Baseline'], width, label='Design 1: Baseline', color='#2b5c8f', edgecolor='black', linewidth=0.8)
rects2 = ax.bar(x - 0.5*width, df1['Design_2_Pipelined'], width, label='Design 2: Pipelined', color='#4682b4', edgecolor='black', linewidth=0.8)
rects3 = ax.bar(x + 0.5*width, df1['Design_3_Renaming'], width, label='Design 3: Renaming', color='#e08214', edgecolor='black', linewidth=0.8, hatch='//')
rects4 = ax.bar(x + 1.5*width, df1['Design_4_Fine_Grained'], width, label='Design 4: Fine-Grained', color='#2ca02c', edgecolor='black', linewidth=0.8)
ax.set_ylabel('Total Execution Cycles (256 iters)')
ax.set_title('Execution Cycles Across PipeIMC Architectural Variants')
ax.set_xticks(x)
ax.set_xticklabels(workloads, fontweight='bold')
ax.legend(frameon=True, facecolor='white', framealpha=0.9)
ax.grid(axis='y', linestyle='--', alpha=0.5)
ax.set_ylim(0, 100000)

for rects in [rects1, rects2, rects3, rects4]:
    for rect in rects:
        h = rect.get_height()
        ax.annotate(f'{int(h):,}', xy=(rect.get_x() + rect.get_width()/2, h),
                    xytext=(0, 3), textcoords="offset points", ha='center', va='bottom', fontsize=7.5, rotation=45)
plt.tight_layout()
plt.savefig('fig1_execution_cycles.png')
plt.savefig('fig1_execution_cycles.pdf')
plt.close()
print("Generated: fig1_execution_cycles.png / .pdf")

# Fig 2
df2 = pd.read_csv('pipeimc_speedup.csv')
fig, ax = plt.subplots(figsize=(7.5, 4.5))
rects1 = ax.bar(x - 1.5*width, df2['Design_1_Baseline'], width, label='Design 1: Baseline', color='#7f7f7f', edgecolor='black')
rects2 = ax.bar(x - 0.5*width, df2['Design_2_Pipelined'], width, label='Design 2: Pipelined', color='#1f77b4', edgecolor='black')
rects3 = ax.bar(x + 0.5*width, df2['Design_3_Renaming'], width, label='Design 3: Renaming', color='#ff7f0e', edgecolor='black', hatch='//')
rects4 = ax.bar(x + 1.5*width, df2['Design_4_Fine_Grained'], width, label='Design 4: Fine-Grained', color='#2ca02c', edgecolor='black')
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
        ax.annotate(f'{h:.2f}x', xy=(rect.get_x() + rect.get_width()/2, h),
                    xytext=(0, 3), textcoords="offset points", ha='center', va='bottom', fontsize=8)
plt.tight_layout()
plt.savefig('fig2_normalized_speedup.png')
plt.savefig('fig2_normalized_speedup.pdf')
plt.close()
print("Generated: fig2_normalized_speedup.png / .pdf")

# Fig 3
df3 = pd.read_csv('pipeimc_per_iteration_latency.csv')
designs = [d.replace('Design ', 'D').split(':')[1].strip() for d in df3['Design']]
idx = np.arange(len(designs))
w = 0.25

fig, ax = plt.subplots(figsize=(8, 4.5))
ax.bar(idx - w, df3['Matmul_Cycles_Per_Iter'], w, label='Matmul', color='#1f77b4', edgecolor='black')
ax.bar(idx, df3['Stencil_Cycles_Per_Iter'], w, label='Stencil', color='#ff7f0e', edgecolor='black')
ax.bar(idx + w, df3['KMeans_Cycles_Per_Iter'], w, label='K-Means', color='#2ca02c', edgecolor='black')
ax.set_ylabel('Cycles per Loop Iteration')
ax.set_title('Per-Iteration Latency Scaling Across Microarchitectural Designs')
ax.set_xticks(idx)
ax.set_xticklabels(designs, fontweight='bold')
ax.legend(frameon=True)
ax.grid(axis='y', linestyle='--', alpha=0.5)
ax.set_ylim(0, 400)

for i in range(len(designs)):
    ax.text(idx[i] - w, df3['Matmul_Cycles_Per_Iter'][i] + 5, f"{df3['Matmul_Cycles_Per_Iter'][i]}c", ha='center', fontsize=8.5)
    ax.text(idx[i], df3['Stencil_Cycles_Per_Iter'][i] + 5, f"{df3['Stencil_Cycles_Per_Iter'][i]}c", ha='center', fontsize=8.5)
    ax.text(idx[i] + w, df3['KMeans_Cycles_Per_Iter'][i] + 5, f"{df3['KMeans_Cycles_Per_Iter'][i]}c", ha='center', fontsize=8.5)
plt.tight_layout()
plt.savefig('fig3_iteration_latency.png')
plt.savefig('fig3_iteration_latency.pdf')
plt.close()
print("Generated: fig3_iteration_latency.png / .pdf")

# Fig 4
df4 = pd.read_csv('pipeimc_scalability_sweep.csv')
iters = df4['Iterations']
fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(14, 4.2), sharex=True)

ax1.plot(iters, df4['Matmul_Baseline'], 'o--', color='#d62728', label='Baseline (341c/it)')
ax1.plot(iters, df4['Matmul_Fine_Grained'], 's-', color='#2ca02c', label='Fine-Grained (223c/it)')
ax1.set_title('Matmul Scalability')
ax1.set_xlabel('Iteration Count')
ax1.set_ylabel('Total Cycles')
ax1.grid(True, linestyle='--', alpha=0.5)
ax1.legend(fontsize=8.5)

ax2.plot(iters, df4['Stencil_Baseline'], 'o--', color='#d62728', label='Baseline (245c/it)')
ax2.plot(iters, df4['Stencil_Pipelined'], '^-', color='#1f77b4', label='Pipelined (223c/it)')
ax2.set_title('Stencil Scalability')
ax2.set_xlabel('Iteration Count')
ax2.grid(True, linestyle='--', alpha=0.5)
ax2.legend(fontsize=8.5)

ax3.plot(iters, df4['KMeans_Baseline'], 'o--', color='#d62728', label='Baseline (204c/it)')
ax3.plot(iters, df4['KMeans_Pipelined'], 'd-', color='#9467bd', label='Pipelined (191c/it)')
ax3.set_title('K-Means Scalability')
ax3.set_xlabel('Iteration Count')
ax3.grid(True, linestyle='--', alpha=0.5)
ax3.legend(fontsize=8.5)

plt.tight_layout()
plt.savefig('fig4_scalability_analysis.png')
plt.savefig('fig4_scalability_analysis.pdf')
plt.close()
print("Generated: fig4_scalability_analysis.png / .pdf")

# Fig 5
df5 = pd.read_csv('pipeimc_matmul_timeline.csv')
fig, (ax_base, ax_fine) = plt.subplots(2, 1, figsize=(12, 5.2), sharex=True)
color_map = {
    'Memory': '#1f77b4',
    'Compute': '#aec7e8',
    'Memory_Overlapped': '#ff7f0e',
    'Memory_Compute': '#d62728',
    'Branch': '#9467bd'
}

df_base = df5[df5['Design'] == 'Baseline']
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
ax_base.set_ylabel('In-Order\\nBaseline', fontweight='bold')
ax_base.set_title('Timeline Comparison for Single Matmul Loop Iteration', fontsize=12)
ax_base.grid(axis='x', linestyle=':', alpha=0.6)
ax_base.axvline(341, color='red', linestyle='--', linewidth=1.2)
ax_base.text(342, 20, 'End: 341c', color='red', fontsize=9, weight='bold')

df_fine = df5[df5['Design'] == 'Fine_Grained']
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
ax_fine.set_ylabel('Fine-Grained\\nIssue', fontweight='bold')
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
print("All tasks completed successfully!")
