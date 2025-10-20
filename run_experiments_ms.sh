#!/bin/bash

# Part C: Performance Experiments Script with Millisecond Precision
# This script runs experiments to compare findsp (multi-process) and findst (multi-threaded) performance

# Compile the programs first
echo "Compiling programs..."
make clean
make

# Create directories for results
mkdir -p experiment_results
mkdir -p test_inputs

# Generate test input files
echo "Generating test input files..."
python3 generate_inputs.py

# Function to run an experiment and measure time in milliseconds
run_experiment_ms() {
    local program=$1
    local input_file=$2
    local M=$3
    local R=$4
    local output_prefix=$5
    
    # Use gtime for better precision on macOS, or time on Linux
    if command -v gtime &> /dev/null; then
        TIME_CMD="gtime"
    else
        TIME_CMD="/usr/bin/time"
    fi
    
    # Run the program and measure execution time in milliseconds
    start_time=$(python3 -c "import time; print(int(time.time() * 1000))")
    $program $input_file $M $R ${output_prefix}_out1.txt ${output_prefix}_out2.txt -1 -1 20 2>&1 > /dev/null
    end_time=$(python3 -c "import time; print(int(time.time() * 1000))")
    
    echo $((end_time - start_time))
}

# Clean up intermediate files
cleanup_files() {
    rm -f split-* intermediate-* output-* *.txt 2>/dev/null
}

# Results file
results_file="experiment_results/results_ms.csv"
echo "Input_Size,Edges,M,R,Program,Time_ms" > $results_file

echo "Starting experiments with millisecond precision..."
echo "=================================================="

# Define experiment parameters - include larger sizes for better measurement
input_files=(
    "test_inputs/input_tiny.txt 100 tiny"
    "test_inputs/input_small.txt 1000 small"
    "test_inputs/input_medium.txt 10000 medium"
    "test_inputs/input_large.txt 100000 large"
    "test_inputs/input_xlarge.txt 500000 xlarge"
)

# M and R configurations to test
m_values=(1 2 4 8 16)
r_values=(1 2 4 8)

# Run each experiment 3 times and take average
NUM_RUNS=3

# Run experiments for each input file
for file_info in "${input_files[@]}"; do
    IFS=' ' read -r input_file num_edges size_name <<< "$file_info"
    
    # Check if file exists
    if [ ! -f "$input_file" ]; then
        echo "Skipping $size_name - file not found"
        continue
    fi
    
    echo ""
    echo "Testing with $size_name input ($num_edges edges)..."
    echo "----------------------------------------"
    
    # Test different M and R combinations
    for M in "${m_values[@]}"; do
        for R in "${r_values[@]}"; do
            echo "  Testing M=$M, R=$R"
            
            # Test multi-process version (findsp)
            total_sp=0
            echo -n "    Running findsp: "
            for run in $(seq 1 $NUM_RUNS); do
                cleanup_files
                time_ms=$(run_experiment_ms ./findsp $input_file $M $R "sp_${size_name}_${M}_${R}")
                total_sp=$((total_sp + time_ms))
                echo -n "$time_ms "
            done
            avg_sp=$((total_sp / NUM_RUNS))
            echo " | Average: ${avg_sp}ms"
            echo "$size_name,$num_edges,$M,$R,findsp,$avg_sp" >> $results_file
            
            # Test multi-threaded version (findst)
            total_st=0
            echo -n "    Running findst: "
            for run in $(seq 1 $NUM_RUNS); do
                cleanup_files
                time_ms=$(run_experiment_ms ./findst $input_file $M $R "st_${size_name}_${M}_${R}")
                total_st=$((total_st + time_ms))
                echo -n "$time_ms "
            done
            avg_st=$((total_st / NUM_RUNS))
            echo " | Average: ${avg_st}ms"
            echo "$size_name,$num_edges,$M,$R,findst,$avg_st" >> $results_file
        done
    done
done

echo ""
echo "Experiments completed! Results saved to $results_file"

# Clean up temporary files
cleanup_files
rm -f sp_*.txt st_*.txt