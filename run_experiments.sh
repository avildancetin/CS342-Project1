#!/bin/bash

# Part C: Performance Experiments Script
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

# Function to run an experiment and measure time
run_experiment() {
    local program=$1
    local input_file=$2
    local M=$3
    local R=$4
    local output_prefix=$5
    
    # Run the program and measure execution time
    /usr/bin/time -p $program $input_file $M $R ${output_prefix}_out1.txt ${output_prefix}_out2.txt -1 -1 20 2>&1 | grep real | awk '{print $2}'
}

# Clean up intermediate files
cleanup_files() {
    rm -f split-* intermediate-* output-* *.txt 2>/dev/null
}

# Results file
results_file="experiment_results/results.csv"
echo "Input_Size,Edges,M,R,Program,Time" > $results_file

echo "Starting experiments..."
echo "======================"

# Define experiment parameters
input_files=(
    "test_inputs/input_tiny.txt 100 tiny"
    "test_inputs/input_small.txt 1000 small"
    "test_inputs/input_medium.txt 10000 medium"
    "test_inputs/input_large.txt 100000 large"
)

# M and R configurations to test
m_values=(1 2 4 8)
r_values=(1 2 4 8)

# Run experiments for each input file
for file_info in "${input_files[@]}"; do
    IFS=' ' read -r input_file num_edges size_name <<< "$file_info"
    
    echo ""
    echo "Testing with $size_name input ($num_edges edges)..."
    echo "----------------------------------------"
    
    # Test different M and R combinations
    for M in "${m_values[@]}"; do
        for R in "${r_values[@]}"; do
            echo "  Testing M=$M, R=$R"
            
            # Test multi-process version (findsp)
            cleanup_files
            echo -n "    Running findsp... "
            time_sp=$(run_experiment ./findsp $input_file $M $R "sp_${size_name}_${M}_${R}")
            echo "$size_name,$num_edges,$M,$R,findsp,$time_sp" >> $results_file
            echo "done ($time_sp seconds)"
            
            # Test multi-threaded version (findst)
            cleanup_files
            echo -n "    Running findst... "
            time_st=$(run_experiment ./findst $input_file $M $R "st_${size_name}_${M}_${R}")
            echo "$size_name,$num_edges,$M,$R,findst,$time_st" >> $results_file
            echo "done ($time_st seconds)"
        done
    done
done

echo ""
echo "Experiments completed! Results saved to $results_file"

# Clean up temporary files
cleanup_files
rm -f sp_*.txt st_*.txt