CS342 Operating Systems - Project 1
====================================

Student Information:
-------------------
Name: [Your Name]
ID: [Your Student ID]
Section: [Your Section]

Project Description:
-------------------
This project implements a MapReduce-style application for processing directed graph edges
to find all source vertices pointing to each destination vertex.

Files Included:
--------------
1. findsp.c - Multi-process implementation using fork() and POSIX shared memory
2. findst.c - Multi-threaded implementation using POSIX threads (pthreads)
3. Makefile - Compilation rules for both programs
4. README.txt - This file
5. in.txt - Sample input file for testing

Compilation:
-----------
To compile both programs:
$ make

To compile individual programs:
$ make findsp    # For multi-process version
$ make findst    # For multi-threaded version

To clean build files:
$ make clean

Usage:
-----
Both programs have the same command-line interface:

./findsp INFILE M R OUT1 OUT2 MIND MAXD SHMSIZE
./findst INFILE M R OUT1 OUT2 MIND MAXD SHMSIZE

Parameters:
- INFILE: Input file containing directed edges (source destination pairs)
- M: Number of mapper processes/threads (1-20)
- R: Number of reducer processes/threads (1-10)
- OUT1: Output file for source lists per destination
- OUT2: Output file for source counts per destination
- MIND: Minimum destination to process (-1 for no limit)
- MAXD: Maximum destination to process (-1 for no limit)
- SHMSIZE: Shared memory size as power of 2 (e.g., 20 means 2^20 bytes)

Example:
-------
./findsp in.txt 3 2 outp1.txt outp2.txt -1 -1 20
./findst in.txt 3 2 outp1.txt outp2.txt -1 -1 20

Testing:
-------
The programs have been tested with the provided sample input and produce
correct output matching the project specifications.

Implementation Notes:
--------------------
- Part A (findsp): Uses fork() for process creation and POSIX shared memory for IPC
- Part B (findst): Uses pthreads for concurrency with mutex synchronization
- Both implementations handle duplicate edge removal automatically
- Intermediate and output files are preserved for verification
- Compatible with Ubuntu 22.04 Linux 64-bit and macOS

Performance:
-----------
For performance analysis and experiments, see report.pdf (Part C)