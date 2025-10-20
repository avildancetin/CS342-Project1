CS342 - Operating Systems Project 1
====================================

STUDENT INFO
------------
Name:
Student ID:
Email:
Section:


COMPILATION
-----------
To compile the programs:
    make

To compile individual programs:
    make findsp
    make findst

To clean compiled binaries:
    make clean


RUNNING
-------
To run the shortest path program:
    ./findsp <graph_file> <source_vertex> <destination_vertex>
    Example: ./findsp tests/tiny.txt 0 5

To run the spanning tree program:
    ./findst <graph_file>
    Example: ./findst tests/tiny.txt


NOTES
-----
- The programs use POSIX threads (-pthread flag)
- Input files should contain "source dest" pairs (one per line)
- Self-loops and duplicate edges are handled appropriately
- Test files are provided in tests/ folder:
  * tiny.txt (10 edges)
  * medium.txt (100 edges)
  * large.txt (1000 edges)


ADDITIONAL INFORMATION
----------------------
(Add any additional notes about your implementation here)


