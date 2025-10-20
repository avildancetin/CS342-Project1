# Complete Reducer Implementation - Step 4 & 5

## Overview
The reducer implementation handles grouping, deduplication, and writing results to both output files and shared memory in text format.

## Step 4: Grouping & Uniqueness

### Algorithm
```
1. Iterate through sorted pair array (sorted by dest, then source)
2. For each unique destination:
   a. Collect all sources
   b. Skip consecutive duplicates (guaranteed by sorting)
   c. Count unique sources
   d. Write to output file
```

### Implementation Details

**Data Structure for Sources:**
```c
uint32_t *sources = malloc(initial_capacity * sizeof(uint32_t));
size_t source_count = 0;
uint32_t last_source = UINT32_MAX;  // Track last seen source
```

**Duplicate Detection:**
```c
while (idx < pairs.count && pairs.pairs[idx].dest == current_dest) {
    uint32_t src = pairs.pairs[idx].source;
    
    // Since sorted, duplicates are consecutive
    if (src != last_source) {
        sources[source_count++] = src;
        last_source = src;
    }
    idx++;
}
```

**Dynamic Array Growth:**
```c
if (source_count >= source_capacity) {
    source_capacity *= 2;
    uint32_t *new_sources = realloc(sources, source_capacity * sizeof(uint32_t));
    if (new_sources == NULL) {
        // Handle malloc failure
        free(sources);
        fclose(output_fp);
        free_pair_array(&pairs);
        exit(EXIT_FAILURE);
    }
    sources = new_sources;
}
```

**Output Format:**
```c
fprintf(output_fp, "%u:", current_dest);
for (size_t j = 0; j < source_count; j++) {
    fprintf(output_fp, " %u", sources[j]);
}
fprintf(output_fp, "\n");
```

## Step 5: Shared Memory Write

### Format Specification
- **Text format:** `"dest count\n"` for each destination
- **Example:** `"3 2\n"` means destination 3 has 2 unique sources

### Implementation

**Calculate Region Bounds:**
```c
if (shm_ptr != NULL) {
    offsets = (uint64_t *)((char *)shm_ptr + sizeof(uint32_t));
    my_offset = offsets[reducer_id];
    my_size = offsets[reducer_id + 1] - my_offset;
    my_region = (char *)shm_ptr + my_offset;
}
```

**Write with Bounds Checking:**
```c
if (my_region != NULL) {
    char shm_line[64];
    int line_len = snprintf(shm_line, sizeof(shm_line), "%u %zu\n", 
                            current_dest, source_count);
    
    // Check if it fits
    if (line_len > 0 && written_bytes + line_len <= my_size) {
        memcpy(my_region + written_bytes, shm_line, line_len);
        written_bytes += line_len;
    } else if (written_bytes + line_len > my_size) {
        fprintf(stderr, "    [Reducer %d] Warning: SHM overflow, skipping dest %u\n",
                reducer_id, current_dest);
    }
}
```

### Bounds Checking Logic
1. Format line with `snprintf()` to temporary buffer
2. Check: `written_bytes + line_len <= my_size`
3. If fits: `memcpy()` to shared memory region
4. If overflow: Log warning and skip (graceful degradation)

## Error Handling Summary

### 1. Malloc Failures
```c
// PairArray allocation
if (arr->pairs == NULL) {
    fprintf(stderr, "Error: Failed to allocate pair array\n");
    return -1;
}

// PairArray expansion
Pair *new_pairs = realloc(arr->pairs, new_capacity * sizeof(Pair));
if (new_pairs == NULL) {
    fprintf(stderr, "Error: Failed to expand pair array\n");
    return -1;
}

// Sources array allocation
sources = malloc(source_capacity * sizeof(uint32_t));
if (sources == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    free_pair_array(&pairs);
    exit(EXIT_FAILURE);
}
```

### 2. File Errors
```c
// Missing intermediate files (graceful)
if (fp == NULL) {
    if (errno == ENOENT) {
        continue;  // Mapper had no data for this reducer
    } else {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n",
                filename, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

// Output file creation failure
if (output_fp == NULL) {
    fprintf(stderr, "Error: Cannot create output file '%s': %s\n",
            output_filename, strerror(errno));
    free_pair_array(&pairs);
    exit(EXIT_FAILURE);
}
```

### 3. SHM Overflow
```c
if (written_bytes + line_len > my_size) {
    fprintf(stderr, "Warning: SHM overflow, skipping dest %u\n", current_dest);
    // Continue processing other destinations
}
```

## Test Results (M=2, R=3, tiny.txt)

### Input Graph (10 edges)
```
0→1, 1→2, 2→3, 3→4, 4→5, 1→3, 0→2, 5→5, 2→4, 1→3 (duplicate)
```

### After Mapping (reversed)
```
1←0, 2←1, 3←2, 4←3, 5←4, 3←1, 2←0, 5←5, 4←2, 3←1 (duplicate)
```

### Reducer 0 (dest % 3 == 0)
**Input:** (3,2), (3,1), (3,1)
**Sorted:** (3,1), (3,1), (3,2)
**After dedup:** dest=3, sources=[1,2]
**Output file:** `3: 1 2\n`
**SHM:** `3 2\n` (4 bytes)

### Reducer 1 (dest % 3 == 1)
**Input:** (1,0), (4,3), (4,2)
**Sorted:** (1,0), (4,2), (4,3)
**After dedup:** dest=1, sources=[0]; dest=4, sources=[2,3]
**Output file:** `1: 0\n4: 2 3\n`
**SHM:** `1 1\n4 2\n` (8 bytes)

### Reducer 2 (dest % 3 == 2)
**Input:** (2,1), (2,0), (5,4), (5,5)
**Sorted:** (2,0), (2,1), (5,4), (5,5)
**After dedup:** dest=2, sources=[0,1]; dest=5, sources=[4,5]
**Output file:** `2: 0 1\n5: 4 5\n`
**SHM:** `2 2\n5 2\n` (8 bytes)

### Verification
✅ All duplicates removed correctly (3←1 appeared twice, deduplicated)
✅ Sources sorted within each destination group
✅ Self-loops handled (5←5)
✅ Text format written to SHM: "dest count\n"
✅ Bounds checking prevents SHM overflow
✅ Graceful handling of missing intermediate files
✅ All malloc failures checked and handled
✅ File errors reported with proper cleanup

## Performance Characteristics

- **Memory:** O(n) where n = total pairs across all intermediate files
- **Time:** O(n log n) for sorting + O(n) for grouping
- **Dynamic growth:** Array doubles when full, amortized O(1) insertion
- **SHM writes:** O(1) per destination, bounded by region size

## Compilation
```bash
# Normal build
make findsp

# Debug build (shows SHM content)
gcc -DDEBUG=1 -Wall -pthread -o findsp_debug findsp.c -lm
./findsp_debug tests/tiny.txt 2 3 out1.txt out2.txt -1 -1 20
```

## Next Steps
1. ✅ Step 4 & 5: Grouping, deduplication, SHM write (DONE)
2. ⏳ Implement `merge_output_files()` - merge output-* → OUT1
3. ⏳ Implement `process_shared_memory()` - read SHM → OUT2
4. ⏳ Implement `validate_arguments()` - check inputs

