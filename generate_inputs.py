#!/usr/bin/env python3

import random
import sys
import os

def generate_input_file(filename, num_edges, max_vertex=100000):
    """
    Generate a random graph input file with specified number of edges.
    """
    with open(filename, 'w') as f:
        for _ in range(num_edges):
            source = random.randint(1, max_vertex)
            dest = random.randint(1, max_vertex)
            f.write(f"{source} {dest}\n")
    
    print(f"Generated {filename} with {num_edges} edges")

if __name__ == "__main__":
    # Create test_inputs directory if it doesn't exist
    if not os.path.exists("test_inputs"):
        os.makedirs("test_inputs")
    
    # Generate input files of different sizes
    sizes = [
        ("tiny", 100, 50),
        ("small", 1000, 100),
        ("medium", 10000, 1000),
        ("large", 100000, 5000),
        ("xlarge", 500000, 10000),
        ("huge", 1000000, 50000)
    ]
    
    for name, num_edges, max_vertex in sizes:
        filename = f"test_inputs/input_{name}.txt"
        generate_input_file(filename, num_edges, max_vertex)