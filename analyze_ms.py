#!/usr/bin/env python3

import csv
import os
from collections import defaultdict

# Read the results
results_file = "experiment_results/results_ms.csv"

if not os.path.exists(results_file):
    print(f"Results file {results_file} not found. Run run_experiments_ms.sh first.")
    exit(1)

# Parse CSV data
data = []
with open(results_file, 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        row['Time_ms'] = int(row['Time_ms'])
        row['M'] = int(row['M'])
        row['R'] = int(row['R'])
        row['Edges'] = int(row['Edges'])
        data.append(row)

print("=" * 80)
print("PERFORMANCE EXPERIMENT RESULTS ANALYSIS (Millisecond Precision)")
print("=" * 80)

# 1. Overall comparison: Process vs Thread
print("\n1. OVERALL COMPARISON: Multi-Process (findsp) vs Multi-Threaded (findst)")
print("-" * 70)

process_times = [d['Time_ms'] for d in data if d['Program'] == 'findsp']
thread_times = [d['Time_ms'] for d in data if d['Program'] == 'findst']

if process_times and thread_times:
    print(f"Average execution time:")
    print(f"  - findsp (processes): {sum(process_times)/len(process_times):.1f} ms")
    print(f"  - findst (threads):   {sum(thread_times)/len(thread_times):.1f} ms")
    
    # Performance difference
    avg_sp = sum(process_times)/len(process_times)
    avg_st = sum(thread_times)/len(thread_times)
    diff = ((avg_st - avg_sp) / avg_sp * 100) if avg_sp > 0 else 0
    print(f"  - Difference: {diff:+.1f}% (positive means threads are slower)")

# 2. Performance by input size
print("\n2. PERFORMANCE BY INPUT SIZE (times in milliseconds)")
print("-" * 70)

size_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
for d in data:
    size_stats[d['Input_Size']][d['Program']].append(d['Time_ms'])

print(f"{'Size':<10} {'Edges':<10} {'findsp (ms)':<15} {'findst (ms)':<15} {'Difference':<15} {'Winner':<10}")
print("-" * 80)
for size in ['tiny', 'small', 'medium', 'large', 'xlarge']:
    if size in size_stats:
        edges = next((d['Edges'] for d in data if d['Input_Size'] == size), 0)
        sp_times = size_stats[size]['findsp']
        st_times = size_stats[size]['findst']
        if sp_times and st_times:
            sp_avg = sum(sp_times) / len(sp_times)
            st_avg = sum(st_times) / len(st_times)
            diff = ((st_avg - sp_avg) / sp_avg * 100) if sp_avg > 0 else 0
            winner = "Process" if sp_avg < st_avg else "Thread" if st_avg < sp_avg else "Tie"
            print(f"{size:<10} {edges:<10} {sp_avg:<15.1f} {st_avg:<15.1f} {diff:+.1f}%{'':<9} {winner:<10}")

# 3. Best and worst configurations for each input size
print("\n3. BEST AND WORST CONFIGURATIONS BY INPUT SIZE")
print("-" * 70)

for size in ['tiny', 'small', 'medium', 'large', 'xlarge']:
    size_data = [d for d in data if d['Input_Size'] == size]
    if size_data:
        sp_data = [d for d in size_data if d['Program'] == 'findsp']
        st_data = [d for d in size_data if d['Program'] == 'findst']
        
        if sp_data and st_data:
            best_sp = min(sp_data, key=lambda x: x['Time_ms'])
            worst_sp = max(sp_data, key=lambda x: x['Time_ms'])
            best_st = min(st_data, key=lambda x: x['Time_ms'])
            worst_st = max(st_data, key=lambda x: x['Time_ms'])
            
            print(f"\n{size.upper()} ({best_sp['Edges']:,} edges):")
            print(f"  findsp:")
            print(f"    Best:  M={best_sp['M']:2d}, R={best_sp['R']:2d} -> {best_sp['Time_ms']:6d} ms")
            print(f"    Worst: M={worst_sp['M']:2d}, R={worst_sp['R']:2d} -> {worst_sp['Time_ms']:6d} ms")
            print(f"    Range: {worst_sp['Time_ms'] - best_sp['Time_ms']} ms ({(worst_sp['Time_ms']/best_sp['Time_ms'] - 1)*100:.1f}% slower)")
            
            print(f"  findst:")
            print(f"    Best:  M={best_st['M']:2d}, R={best_st['R']:2d} -> {best_st['Time_ms']:6d} ms")
            print(f"    Worst: M={worst_st['M']:2d}, R={worst_st['R']:2d} -> {worst_st['Time_ms']:6d} ms")
            print(f"    Range: {worst_st['Time_ms'] - best_st['Time_ms']} ms ({(worst_st['Time_ms']/best_st['Time_ms'] - 1)*100:.1f}% slower)")

# 4. Effect of varying M (mappers) - for large dataset
print("\n4. EFFECT OF NUMBER OF MAPPERS (M) - Large dataset, Fixed R=2")
print("-" * 70)

large_r2 = [d for d in data if d['Input_Size'] == 'large' and d['R'] == 2]
if large_r2:
    m_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
    for d in large_r2:
        m_stats[d['M']][d['Program']].append(d['Time_ms'])

    print(f"{'M':<5} {'findsp (ms)':<15} {'findst (ms)':<15} {'Speedup (sp)':<15} {'Speedup (st)':<15}")
    print("-" * 65)

    # Get baseline (M=1)
    baseline_sp = sum(m_stats[1]['findsp']) / len(m_stats[1]['findsp']) if 1 in m_stats and m_stats[1]['findsp'] else 1
    baseline_st = sum(m_stats[1]['findst']) / len(m_stats[1]['findst']) if 1 in m_stats and m_stats[1]['findst'] else 1

    for m in sorted(m_stats.keys()):
        if m_stats[m]['findsp'] and m_stats[m]['findst']:
            sp_avg = sum(m_stats[m]['findsp']) / len(m_stats[m]['findsp'])
            st_avg = sum(m_stats[m]['findst']) / len(m_stats[m]['findst'])
            sp_speedup = baseline_sp / sp_avg if sp_avg > 0 else 0
            st_speedup = baseline_st / st_avg if st_avg > 0 else 0
            print(f"{m:<5} {sp_avg:<15.1f} {st_avg:<15.1f} {sp_speedup:<15.2f} {st_speedup:<15.2f}")

# 5. Effect of varying R (reducers) - for large dataset
print("\n5. EFFECT OF NUMBER OF REDUCERS (R) - Large dataset, Fixed M=4")
print("-" * 70)

large_m4 = [d for d in data if d['Input_Size'] == 'large' and d['M'] == 4]
if large_m4:
    r_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
    for d in large_m4:
        r_stats[d['R']][d['Program']].append(d['Time_ms'])

    print(f"{'R':<5} {'findsp (ms)':<15} {'findst (ms)':<15} {'Overhead vs R=1':<20} {'Overhead vs R=1':<20}")
    print("-" * 75)

    # Get baseline (R=1)
    baseline_sp = sum(r_stats[1]['findsp']) / len(r_stats[1]['findsp']) if 1 in r_stats and r_stats[1]['findsp'] else 1
    baseline_st = sum(r_stats[1]['findst']) / len(r_stats[1]['findst']) if 1 in r_stats and r_stats[1]['findst'] else 1

    for r in sorted(r_stats.keys()):
        if r_stats[r]['findsp'] and r_stats[r]['findst']:
            sp_avg = sum(r_stats[r]['findsp']) / len(r_stats[r]['findsp'])
            st_avg = sum(r_stats[r]['findst']) / len(r_stats[r]['findst'])
            sp_overhead = ((sp_avg - baseline_sp) / baseline_sp * 100) if baseline_sp > 0 else 0
            st_overhead = ((st_avg - baseline_st) / baseline_st * 100) if baseline_st > 0 else 0
            print(f"{r:<5} {sp_avg:<15.1f} {st_avg:<15.1f} {sp_overhead:+.1f}%{'':<14} {st_overhead:+.1f}%")

# 6. Scalability Analysis with actual millisecond times
print("\n6. SCALABILITY ANALYSIS - XLarge dataset (500,000 edges)")
print("-" * 70)

xlarge_data = [d for d in data if d['Input_Size'] == 'xlarge']
if xlarge_data:
    worker_stats = defaultdict(lambda: {'findsp': [], 'findst': []})
    for d in xlarge_data:
        total_workers = d['M'] + d['R']
        worker_stats[total_workers][d['Program']].append(d['Time_ms'])

    print(f"{'Workers':<10} {'findsp (ms)':<15} {'findst (ms)':<15} {'Process Eff':<15} {'Thread Eff':<15}")
    print("-" * 70)

    # Get baseline (2 workers - M=1, R=1)
    baseline_data = [d for d in xlarge_data if d['M'] == 1 and d['R'] == 1]
    if baseline_data:
        sp_baseline_times = [d['Time_ms'] for d in baseline_data if d['Program'] == 'findsp']
        st_baseline_times = [d['Time_ms'] for d in baseline_data if d['Program'] == 'findst']
        
        if sp_baseline_times and st_baseline_times:
            baseline_sp = sum(sp_baseline_times) / len(sp_baseline_times)
            baseline_st = sum(st_baseline_times) / len(st_baseline_times)

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
                    print(f"{workers:<10} {sp_avg:<15.1f} {st_avg:<15.1f} {sp_efficiency:<14.1f}% {st_efficiency:<14.1f}%")

# 7. Statistical summary
print("\n7. STATISTICAL SUMMARY")
print("-" * 70)

for size in ['tiny', 'small', 'medium', 'large', 'xlarge']:
    size_data = [d for d in data if d['Input_Size'] == size]
    if size_data:
        sp_times = [d['Time_ms'] for d in size_data if d['Program'] == 'findsp']
        st_times = [d['Time_ms'] for d in size_data if d['Program'] == 'findst']
        
        if sp_times and st_times:
            # Calculate variance
            sp_mean = sum(sp_times) / len(sp_times)
            st_mean = sum(st_times) / len(st_times)
            sp_variance = sum((x - sp_mean) ** 2 for x in sp_times) / len(sp_times)
            st_variance = sum((x - st_mean) ** 2 for x in st_times) / len(st_times)
            sp_std = sp_variance ** 0.5
            st_std = st_variance ** 0.5
            
            print(f"\n{size.upper()}:")
            print(f"  findsp: mean={sp_mean:.1f}ms, std={sp_std:.1f}ms, min={min(sp_times)}ms, max={max(sp_times)}ms")
            print(f"  findst: mean={st_mean:.1f}ms, std={st_std:.1f}ms, min={min(st_times)}ms, max={max(st_times)}ms")

print("\n" + "=" * 80)