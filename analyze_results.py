#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# Read the results
results_file = "experiment_results/results.csv"

if not os.path.exists(results_file):
    print(f"Results file {results_file} not found. Run run_experiments.sh first.")
    exit(1)

df = pd.read_csv(results_file)

# Create output directory for plots
os.makedirs("experiment_results/plots", exist_ok=True)

# 1. Process vs Thread comparison across different input sizes
fig, axes = plt.subplots(2, 2, figsize=(12, 10))
fig.suptitle('Multi-Process (findsp) vs Multi-Threaded (findst) Performance')

# Group by input size and program type
size_order = ['tiny', 'small', 'medium', 'large']
available_sizes = [s for s in size_order if s in df['Input_Size'].unique()]

# Plot 1: Time vs Input Size for fixed M and R
ax = axes[0, 0]
fixed_m = 4
fixed_r = 4
subset = df[(df['M'] == fixed_m) & (df['R'] == fixed_r)]
for prog in ['findsp', 'findst']:
    prog_data = subset[subset['Program'] == prog]
    prog_data = prog_data.set_index('Input_Size').reindex(available_sizes)
    ax.plot(range(len(available_sizes)), prog_data['Time'], 
            marker='o', label=prog, linewidth=2)
ax.set_xticks(range(len(available_sizes)))
ax.set_xticklabels(available_sizes)
ax.set_xlabel('Input Size')
ax.set_ylabel('Time (seconds)')
ax.set_title(f'Execution Time vs Input Size (M={fixed_m}, R={fixed_r})')
ax.legend()
ax.grid(True, alpha=0.3)

# Plot 2: Time vs Number of Mappers
ax = axes[0, 1]
fixed_r = 4
subset = df[df['R'] == fixed_r]
for size in available_sizes[:3]:  # Use first 3 sizes for clarity
    size_data = subset[subset['Input_Size'] == size]
    avg_by_m = size_data.groupby(['M', 'Program'])['Time'].mean().unstack()
    if 'findsp' in avg_by_m.columns:
        ax.plot(avg_by_m.index, avg_by_m['findsp'], 
                marker='o', label=f'{size} (process)', linestyle='-')
    if 'findst' in avg_by_m.columns:
        ax.plot(avg_by_m.index, avg_by_m['findst'], 
                marker='s', label=f'{size} (thread)', linestyle='--')
ax.set_xlabel('Number of Mappers (M)')
ax.set_ylabel('Time (seconds)')
ax.set_title(f'Execution Time vs Number of Mappers (R={fixed_r})')
ax.legend()
ax.grid(True, alpha=0.3)

# Plot 3: Time vs Number of Reducers
ax = axes[1, 0]
fixed_m = 4
subset = df[df['M'] == fixed_m]
for size in available_sizes[:3]:  # Use first 3 sizes for clarity
    size_data = subset[subset['Input_Size'] == size]
    avg_by_r = size_data.groupby(['R', 'Program'])['Time'].mean().unstack()
    if 'findsp' in avg_by_r.columns:
        ax.plot(avg_by_r.index, avg_by_r['findsp'], 
                marker='o', label=f'{size} (process)', linestyle='-')
    if 'findst' in avg_by_r.columns:
        ax.plot(avg_by_r.index, avg_by_r['findst'], 
                marker='s', label=f'{size} (thread)', linestyle='--')
ax.set_xlabel('Number of Reducers (R)')
ax.set_ylabel('Time (seconds)')
ax.set_title(f'Execution Time vs Number of Reducers (M={fixed_m})')
ax.legend()
ax.grid(True, alpha=0.3)

# Plot 4: Speedup Analysis
ax = axes[1, 1]
for size in available_sizes[:3]:
    size_data = df[df['Input_Size'] == size]
    # Calculate speedup relative to M=1, R=1
    baseline = size_data[(size_data['M'] == 1) & (size_data['R'] == 1)]
    
    for prog in ['findsp', 'findst']:
        prog_data = size_data[size_data['Program'] == prog]
        baseline_time = baseline[baseline['Program'] == prog]['Time'].mean()
        
        if baseline_time > 0:
            speedups = []
            total_workers = []
            for _, row in prog_data.iterrows():
                speedup = baseline_time / row['Time'] if row['Time'] > 0 else 0
                speedups.append(speedup)
                total_workers.append(row['M'] + row['R'])
            
            label = f'{size} ({prog.replace("find", "")})'
            marker = 'o' if prog == 'findsp' else 's'
            linestyle = '-' if prog == 'findsp' else '--'
            ax.scatter(total_workers, speedups, label=label, marker=marker, alpha=0.7)

# Add ideal speedup line
max_workers = df['M'].max() + df['R'].max()
ax.plot([1, max_workers], [1, max_workers], 'k--', alpha=0.3, label='Ideal Speedup')
ax.set_xlabel('Total Workers (M + R)')
ax.set_ylabel('Speedup')
ax.set_title('Speedup Analysis')
ax.legend()
ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig('experiment_results/plots/performance_comparison.png', dpi=150)
plt.show()

# Generate summary statistics
print("\n=== Performance Summary ===\n")
print("Average Execution Times by Program Type:")
print(df.groupby('Program')['Time'].agg(['mean', 'std', 'min', 'max']))

print("\n\nAverage Times by Input Size and Program:")
pivot_table = df.pivot_table(values='Time', index='Input_Size', 
                             columns='Program', aggfunc='mean')
pivot_table = pivot_table.reindex(available_sizes)
print(pivot_table)

print("\n\nBest Configurations by Input Size:")
for size in available_sizes:
    size_data = df[df['Input_Size'] == size]
    best_sp = size_data[size_data['Program'] == 'findsp'].nsmallest(1, 'Time')
    best_st = size_data[size_data['Program'] == 'findst'].nsmallest(1, 'Time')
    
    print(f"\n{size.upper()}:")
    if not best_sp.empty:
        print(f"  Best findsp: M={best_sp.iloc[0]['M']}, R={best_sp.iloc[0]['R']}, "
              f"Time={best_sp.iloc[0]['Time']:.3f}s")
    if not best_st.empty:
        print(f"  Best findst: M={best_st.iloc[0]['M']}, R={best_st.iloc[0]['R']}, "
              f"Time={best_st.iloc[0]['Time']:.3f}s")