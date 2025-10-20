#!/usr/bin/env python3

import csv
import os
from collections import defaultdict

# Read the results
results_file = "experiment_results/results.csv"

if not os.path.exists(results_file):
    print(f"Results file {results_file} not found. Run run_experiments.sh first.")
    exit(1)

# Parse CSV data
data = []
with open(results_file, 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        row['Time'] = float(row['Time'])
        row['M'] = int(row['M'])
        row['R'] = int(row['R'])
        row['Edges'] = int(row['Edges'])
        data.append(row)

print("=" * 70)
print("PERFORMANCE EXPERIMENT RESULTS ANALYSIS")
print("=" * 70)

# 1. Overall comparison: Process vs Thread
print("\n1. OVERALL COMPARISON: Multi-Process (findsp) vs Multi-Threaded (findst)")
print("-" * 60)

process_times = [d['Time'] for d in data if d['Program'] == 'findsp']
thread_times = [d['Time'] for d in data if d['Program'] == 'findst']

print(f"Average execution time:")
print(f"  - findsp (processes): {sum(process_times)/len(process_times):.4f} seconds")
print(f"  - findst (threads):   {sum(thread_times)/len(thread_times):.4f} seconds")

# 2. Performance by input size
print("\n2. PERFORMANCE BY INPUT SIZE (average times in seconds)")
print("-" * 60)

size_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
for d in data:
    size_stats[d['Input_Size']][d['Program']].append(d['Time'])

print(f"{'Size':<10} {'Edges':<10} {'findsp (avg)':<15} {'findst (avg)':<15} {'Difference':<10}")
print("-" * 65)
for size in ['tiny', 'small', 'medium', 'large']:
    if size in size_stats:
        edges = next((d['Edges'] for d in data if d['Input_Size'] == size), 0)
        sp_avg = sum(size_stats[size]['findsp']) / len(size_stats[size]['findsp'])
        st_avg = sum(size_stats[size]['findst']) / len(size_stats[size]['findst'])
        diff = ((st_avg - sp_avg) / sp_avg * 100) if sp_avg > 0 else 0
        print(f"{size:<10} {edges:<10} {sp_avg:<15.4f} {st_avg:<15.4f} {diff:+.1f}%")

# 3. Best configurations for each input size
print("\n3. BEST CONFIGURATIONS BY INPUT SIZE")
print("-" * 60)

for size in ['tiny', 'small', 'medium', 'large']:
    size_data = [d for d in data if d['Input_Size'] == size]
    if size_data:
        best_sp = min([d for d in size_data if d['Program'] == 'findsp'], key=lambda x: x['Time'])
        best_st = min([d for d in size_data if d['Program'] == 'findst'], key=lambda x: x['Time'])
        
        print(f"\n{size.upper()} ({best_sp['Edges']} edges):")
        print(f"  Best findsp: M={best_sp['M']}, R={best_sp['R']}, Time={best_sp['Time']:.4f}s")
        print(f"  Best findst: M={best_st['M']}, R={best_st['R']}, Time={best_st['Time']:.4f}s")

# 4. Effect of varying M (mappers)
print("\n4. EFFECT OF NUMBER OF MAPPERS (M) - Fixed R=4, medium dataset")
print("-" * 60)

medium_r4 = [d for d in data if d['Input_Size'] == 'medium' and d['R'] == 4]
m_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
for d in medium_r4:
    m_stats[d['M']][d['Program']].append(d['Time'])

print(f"{'M':<5} {'findsp (avg)':<15} {'findst (avg)':<15} {'Speedup (sp)':<15} {'Speedup (st)':<15}")
print("-" * 65)

# Get baseline (M=1)
baseline_sp = sum(m_stats[1]['findsp']) / len(m_stats[1]['findsp']) if 1 in m_stats else 1
baseline_st = sum(m_stats[1]['findst']) / len(m_stats[1]['findst']) if 1 in m_stats else 1

for m in sorted(m_stats.keys()):
    sp_avg = sum(m_stats[m]['findsp']) / len(m_stats[m]['findsp'])
    st_avg = sum(m_stats[m]['findst']) / len(m_stats[m]['findst'])
    sp_speedup = baseline_sp / sp_avg if sp_avg > 0 else 0
    st_speedup = baseline_st / st_avg if st_avg > 0 else 0
    print(f"{m:<5} {sp_avg:<15.4f} {st_avg:<15.4f} {sp_speedup:<15.2f} {st_speedup:<15.2f}")

# 5. Effect of varying R (reducers)
print("\n5. EFFECT OF NUMBER OF REDUCERS (R) - Fixed M=4, medium dataset")
print("-" * 60)

medium_m4 = [d for d in data if d['Input_Size'] == 'medium' and d['M'] == 4]
r_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
for d in medium_m4:
    r_stats[d['R']][d['Program']].append(d['Time'])

print(f"{'R':<5} {'findsp (avg)':<15} {'findst (avg)':<15} {'Speedup (sp)':<15} {'Speedup (st)':<15}")
print("-" * 65)

# Get baseline (R=1)
baseline_sp = sum(r_stats[1]['findsp']) / len(r_stats[1]['findsp']) if 1 in r_stats else 1
baseline_st = sum(r_stats[1]['findst']) / len(r_stats[1]['findst']) if 1 in r_stats else 1

for r in sorted(r_stats.keys()):
    sp_avg = sum(r_stats[r]['findsp']) / len(r_stats[r]['findsp'])
    st_avg = sum(r_stats[r]['findst']) / len(r_stats[r]['findst'])
    sp_speedup = baseline_sp / sp_avg if sp_avg > 0 else 0
    st_speedup = baseline_st / st_avg if st_avg > 0 else 0
    print(f"{r:<5} {sp_avg:<15.4f} {st_avg:<15.4f} {sp_speedup:<15.2f} {st_speedup:<15.2f}")

# 6. Scalability Analysis
print("\n6. SCALABILITY ANALYSIS - Large dataset (100000 edges)")
print("-" * 60)

large_data = [d for d in data if d['Input_Size'] == 'large']
worker_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
for d in large_data:
    total_workers = d['M'] + d['R']
    worker_stats[total_workers][d['Program']].append(d['Time'])

print(f"{'Workers':<10} {'findsp (avg)':<15} {'findst (avg)':<15} {'Efficiency (sp)':<18} {'Efficiency (st)':<18}")
print("-" * 76)

# Get baseline (2 workers - M=1, R=1)
baseline_data = [d for d in large_data if d['M'] == 1 and d['R'] == 1]
baseline_sp = sum([d['Time'] for d in baseline_data if d['Program'] == 'findsp']) / len([d for d in baseline_data if d['Program'] == 'findsp'])
baseline_st = sum([d['Time'] for d in baseline_data if d['Program'] == 'findst']) / len([d for d in baseline_data if d['Program'] == 'findst'])

for workers in sorted(worker_stats.keys()):
    sp_times = worker_stats[workers]['findsp']
    st_times = worker_stats[workers]['findst']
    if sp_times and st_times:
        sp_avg = sum(sp_times) / len(sp_times)
        st_avg = sum(st_times) / len(st_times)
        sp_speedup = baseline_sp / sp_avg if sp_avg > 0 else 0
        st_speedup = baseline_st / st_avg if st_avg > 0 else 0
        sp_efficiency = (sp_speedup / workers * 100) if workers > 0 else 0
        st_efficiency = (st_speedup / workers * 100) if workers > 0 else 0
        print(f"{workers:<10} {sp_avg:<15.4f} {st_avg:<15.4f} {sp_efficiency:<17.1f}% {st_efficiency:<17.1f}%")

print("\n" + "=" * 70)