# Reducer Implementation Summary

## Data Structures

```c
typedef struct {
    uint32_t dest;
    uint32_t source;
} Pair;

typedef struct {
    Pair *pairs;
    size_t count;
    size_t capacity;
} PairArray;
```

## Functions Implemented

### 1. `init_pair_array(PairArray *arr)`
- Initializes dynamic array with capacity 1024
- Allocates memory for pairs

### 2. `add_pair(PairArray *arr, uint32_t dest, uint32_t source)`
- Adds pair to array
- Doubles capacity when full (dynamic growth)
- Returns 0 on success, -1 on failure

### 3. `free_pair_array(PairArray *arr)`
- Frees allocated memory
- Resets count and capacity

### 4. `compare_pairs(const void *a, const void *b)`
- Comparator for qsort()
- Primary sort: by dest (ascending)
- Secondary sort: by source (ascending)

### 5. `reducer_process(int reducer_id, int M, int R, void *shm_ptr)`

**Step 1: Read intermediate files**
- Reads `intermediate-i-<reducer_id>` for i = 0 to M-1
- Handles missing files gracefully (ENOENT)
- Stores all (dest, source) pairs in PairArray

**Step 2: Sort pairs**
- Uses qsort() with compare_pairs comparator
- Ensures pairs grouped by destination
- Within each group, sources are sorted

**Step 3: Create output file**
- Opens `output-<reducer_id>`
- Ready for writing grouped results

**Step 4: Get shared memory region**
- Reads offsets from SHM header
- Calculates this reducer's region: [offsets[id], offsets[id+1])
- Gets pointer to writable region

**Step 5: Group, deduplicate, and write**
- Iterates through sorted pairs
- For each unique destination:
  - Collects all sources
  - Removes duplicates (consecutive due to sorting)
  - Writes to output file: "dest: s1 s2 s3\n"
  - Writes to SHM: [dest (4B)][count (4B)]
- Tracks bytes written to SHM

**Step 6: Cleanup**
- Closes output file
- Frees pair array
- Exits successfully

### 6. `fork_reducers(int R, int M)`
- Allocates PID array for R reducers
- Forks R child processes
- Each child calls `reducer_process()`
- Parent stores PIDs for waiting
- Retry logic on fork failures

## Test Results (M=2, R=3, tiny.txt)

### Input
```
0 1, 1 2, 2 3, 3 4, 4 5, 1 3, 0 2, 5 5, 2 4, 1 3
```

### After Mapper (reversed pairs)
```
1 0, 2 1, 3 2, 4 3, 5 4, 3 1, 2 0, 5 5, 4 2, 3 1
```

### Distribution to Reducers (by dest % 3)

**Reducer 0 (dest % 3 == 0):**
- Pairs: (3,2), (3,1), (3,1)
- Sorted: (3,1), (3,1), (3,2)
- Output: `3: 1 2`
- SHM: [3][2] = 8 bytes

**Reducer 1 (dest % 3 == 1):**
- Pairs: (1,0), (4,3), (4,2)
- Sorted: (1,0), (4,2), (4,3)
- Output: `1: 0\n4: 2 3`
- SHM: [1][1][4][2] = 16 bytes

**Reducer 2 (dest % 3 == 2):**
- Pairs: (2,1), (2,0), (5,4), (5,5)
- Sorted: (2,0), (2,1), (5,4), (5,5)
- Output: `2: 0 1\n5: 4 5`
- SHM: [2][2][5][2] = 16 bytes

### Verification
```
✓ Reducer 0: Wrote 1 destinations, 8 bytes to SHM
✓ Reducer 1: Wrote 2 destinations, 16 bytes to SHM
✓ Reducer 2: Wrote 2 destinations, 16 bytes to SHM
✓ All reducers completed successfully
✓ Duplicate removal working (3:1 appeared twice, deduplicated)
✓ Self-loops handled correctly (5:5)
```

## Key Features

1. **Dynamic memory management**: Arrays grow as needed
2. **Efficient sorting**: Uses built-in qsort()
3. **Duplicate removal**: Consecutive duplicates removed after sorting
4. **Error handling**: Missing files, memory allocation failures, file I/O errors
5. **Shared memory integration**: Each reducer writes to its designated region
6. **Process isolation**: Each reducer runs independently in child process
7. **Graceful degradation**: Handles empty intermediate files

## Compilation
```bash
make findsp
# With debug mode:
gcc -DDEBUG=1 -Wall -pthread -o findsp_debug findsp.c -lm
```

## Next Steps
- Implement `merge_output_files()` - merge output-* into OUT1
- Implement `process_shared_memory()` - read SHM, sort, write OUT2
- Implement `validate_arguments()` - validate M, R, INFILE, SHMSIZE


