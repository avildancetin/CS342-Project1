/*
 * findsp.c - MapReduce-based file processing program
 * CS342 Operating Systems Project 1
 *
 * This program splits an input file among M mapper processes,
 * then uses R reducer processes to aggregate results using
 * POSIX shared memory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>

/* Debug macro - set to 1 to enable debug logging */
#ifndef DEBUG
#define DEBUG 0
#endif

#define DEBUG_LOG(mapper_id, fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[DEBUG][Mapper %d] " fmt, mapper_id, ##__VA_ARGS__); } while (0)

/* Constants */
#define MAX_MAPPERS 20
#define MAX_REDUCERS 10
#define MAX_PATH 256
#define SHM_NAME_PREFIX "/findsp_shm_"
#define INITIAL_PAIR_CAPACITY 1024
#define INITIAL_DESTCOUNT_CAPACITY 1024

/* Data structures */
typedef struct {
    uint32_t dest;
    uint32_t source;
} Pair;

typedef struct {
    Pair *pairs;
    size_t count;
    size_t capacity;
} PairArray;

typedef struct {
    uint32_t dest;
    uint32_t count;
} DestCount;

typedef struct {
    DestCount *entries;
    size_t count;
    size_t capacity;
} DestCountArray;

/* Global variables */
char *shm_ptr = NULL;
int shm_fd = -1;
size_t shm_size = 0;
char shm_name[MAX_PATH];  /* Shared memory object name */
pid_t *child_pids = NULL;  /* Array to store child process PIDs */

/* Helper function declarations */

/**
 * Validates command-line arguments
 * Returns 0 on success, -1 on error
 */
int validate_arguments(int M, int R, const char *infile, int shmsize);

/**
 * Splits input file into M split files (split-0, split-1, ..., split-M-1)
 * Uses round-robin distribution
 * Filters based on MIND/MAXD (-1 means no filtering)
 * Returns 0 on success, -1 on error
 */
int split_input_file(const char *infile, int M, int MIND, int MAXD);

/**
 * Mapper process function - processes one split file
 * Reads split-<mapper_id>, writes to intermediate-<mapper_id>-k files
 */
void mapper_process(int mapper_id, int M, int R);

/**
 * Forks M mapper child processes
 * Each mapper processes its corresponding split-k file
 * Returns 0 on success, -1 on error
 */
int fork_mappers(int M, int R);

/**
 * Creates POSIX shared memory and returns the mapped pointer
 * Size = 1ULL << SHMSIZE
 * shm_name will be set to the created shared memory name
 * Returns pointer to mapped memory, or NULL on error
 */
void* create_shared_memory_region(int SHMSIZE, int R);

/**
 * Initializes the shared memory layout
 * Layout: [uint32_t R][uint64_t offsets[R+1]][data regions]
 */
void initialize_shm_layout(void *shm_ptr, int R, size_t total_size);

/**
 * Cleanup shared memory - unmaps and unlinks
 */
void cleanup_shared_memory(void *shm_ptr, size_t size, const char *shm_name);

/**
 * Wrapper for backward compatibility (calls create_shared_memory_region)
 * Returns 0 on success, -1 on error
 */
int create_shared_memory(int shmsize, int R);

/**
 * Reducer process function - processes intermediate files from all mappers
 * Reads intermediate-*-<reducer_id>, sorts, groups, and writes output
 */
void reducer_process(int reducer_id, int M, int R, void *shm_ptr);

/**
 * Forks R reducer child processes
 * Each reducer reads from shared memory and processes data
 * Returns 0 on success, -1 on error
 */
int fork_reducers(int R, int M);

/**
 * Merges all output-k files into OUT1 (R-way merge, sorted by destination)
 * Returns 0 on success, -1 on error
 */
int merge_output_files(int R, const char *out1);

/**
 * Reads data from shared memory, sorts it, and writes to OUT2
 * Returns 0 on success, -1 on error
 */
int process_shared_memory(const char *out2);

/**
 * Cleanup function - unlinks shared memory and removes temporary files
 */
void cleanup(int M, int R);

/**
 * Wait for all child processes to complete
 * Returns 0 on success, -1 if any child failed
 */
int wait_for_children(int num_children);


#ifndef TEST_MODE
int main(int argc, char *argv[]) {
    /* Command-line arguments */
    char *infile;
    int M, R;
    char *out1, *out2;
    int MIND, MAXD, SHMSIZE;
    
    /* Check argument count */
    if (argc != 9) {
        fprintf(stderr, "Usage: %s INFILE M R OUT1 OUT2 MIND MAXD SHMSIZE\n", argv[0]);
        fprintf(stderr, "  INFILE  : Input file path\n");
        fprintf(stderr, "  M       : Number of mapper processes [1-20]\n");
        fprintf(stderr, "  R       : Number of reducer processes [1-10]\n");
        fprintf(stderr, "  OUT1    : First output file\n");
        fprintf(stderr, "  OUT2    : Second output file\n");
        fprintf(stderr, "  MIND    : Minimum distance parameter\n");
        fprintf(stderr, "  MAXD    : Maximum distance parameter\n");
        fprintf(stderr, "  SHMSIZE : Shared memory size (2^SHMSIZE bytes)\n");
        exit(EXIT_FAILURE);
    }
    
    /* Parse arguments */
    infile = argv[1];
    M = atoi(argv[2]);
    R = atoi(argv[3]);
    out1 = argv[4];
    out2 = argv[5];
    MIND = atoi(argv[6]);
    MAXD = atoi(argv[7]);
    SHMSIZE = atoi(argv[8]);
    
    printf("Starting findsp with parameters:\n");
    printf("  Input file: %s\n", infile);
    printf("  Mappers: %d, Reducers: %d\n", M, R);
    printf("  Output files: %s, %s\n", out1, out2);
    printf("  Distance range: [%d, %d]\n", MIND, MAXD);
    printf("  Shared memory size: 2^%d = %zu bytes\n", SHMSIZE, (size_t)pow(2, SHMSIZE));
    
    /* Step 1: Validate arguments */
    printf("\n[Step 1] Validating arguments...\n");
    if (validate_arguments(M, R, infile, SHMSIZE) != 0) {
        fprintf(stderr, "Error: Invalid arguments\n");
        exit(EXIT_FAILURE);
    }
    printf("  Arguments validated successfully\n");
    
    /* Step 2: Split input file into M parts (round-robin) */
    printf("\n[Step 2] Splitting input file into %d parts...\n", M);
    if (split_input_file(infile, M, MIND, MAXD) != 0) {
        fprintf(stderr, "Error: Failed to split input file\n");
        exit(EXIT_FAILURE);
    }
    printf("  Input file split successfully\n");
    
    /* Step 3: Fork M mapper child processes */
    printf("\n[Step 3] Forking %d mapper processes...\n", M);
    if (fork_mappers(M, R) != 0) {
        fprintf(stderr, "Error: Failed to fork mappers\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  Mapper processes forked\n");
    
    /* Step 4: Wait for all mapper processes */
    printf("\n[Step 4] Waiting for mapper processes to complete...\n");
    if (wait_for_children(M) != 0) {
        fprintf(stderr, "Error: Some mapper processes failed\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  All mapper processes completed successfully\n");
    
    /* Step 5: Create POSIX shared memory */
    printf("\n[Step 5] Creating shared memory segment...\n");
    if (create_shared_memory(SHMSIZE, R) != 0) {
        fprintf(stderr, "Error: Failed to create shared memory\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  Shared memory created successfully (name: %s, size: %zu bytes)\n", shm_name, shm_size);
    
    /* Step 6: Fork R reducer child processes */
    printf("\n[Step 6] Forking %d reducer processes...\n", R);
    if (fork_reducers(R, M) != 0) {
        fprintf(stderr, "Error: Failed to fork reducers\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  Reducer processes forked\n");
    
    /* Step 7: Wait for all reducer processes */
    printf("\n[Step 7] Waiting for reducer processes to complete...\n");
    if (wait_for_children(R) != 0) {
        fprintf(stderr, "Error: Some reducer processes failed\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  All reducer processes completed successfully\n");
    
    /* Step 8: Merge output-k files into OUT1 */
    printf("\n[Step 8] Merging output files into %s...\n", out1);
    if (merge_output_files(R, out1) != 0) {
        fprintf(stderr, "Error: Failed to merge output files\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  Output files merged successfully\n");
    
    /* Step 9: Read shared memory, sort, and write to OUT2 */
    printf("\n[Step 9] Processing shared memory and writing to %s...\n", out2);
    
    /* Debug: Print SHM content */
    if (DEBUG && shm_ptr != NULL) {
        uint64_t *offsets = (uint64_t *)((char *)shm_ptr + sizeof(uint32_t));
        for (int i = 0; i < R; i++) {
            size_t region_start = offsets[i];
            size_t region_size = offsets[i + 1] - region_start;
            char *region = (char *)shm_ptr + region_start;
            
            fprintf(stderr, "\n[DEBUG] Reducer %d SHM content (%zu bytes):\n", i, region_size);
            size_t data_len = 0;
            for (size_t j = 0; j < region_size && region[j] != '\0'; j++) {
                data_len++;
            }
            if (data_len > 0) {
                fwrite(region, 1, data_len, stderr);
                fprintf(stderr, "[END]\n");
            } else {
                fprintf(stderr, "(empty)\n");
            }
        }
    }
    
    if (process_shared_memory(out2) != 0) {
        fprintf(stderr, "Error: Failed to process shared memory\n");
        cleanup(M, R);
        exit(EXIT_FAILURE);
    }
    printf("  Shared memory processed and written to %s\n", out2);
    
    /* Step 10: Cleanup */
    printf("\n[Step 10] Cleaning up...\n");
    cleanup(M, R);
    printf("  Cleanup complete\n");
    
    printf("\nProgram completed successfully!\n");
    return EXIT_SUCCESS;
}
#endif /* TEST_MODE */


/* Function implementations */

int validate_arguments(int M, int R, const char *infile, int shmsize) {
    // TODO: Implement validation
    // - Check M is in range [1, 20]
    // - Check R is in range [1, 10]
    // - Check if INFILE exists and is readable
    // - Check SHMSIZE is valid (reasonable range, e.g., [10, 30])
    return 0;
}

int split_input_file(const char *infile, int M, int MIND, int MAXD) {
    FILE *input_fp = NULL;
    FILE **split_fps = NULL;
    char line[512];
    int source, dest;
    int line_num = 0;
    int ret = 0;
    int i;
    
    /* Open input file */
    input_fp = fopen(infile, "r");
    if (input_fp == NULL) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n", infile, strerror(errno));
        return -1;
    }
    
    /* Allocate array of file pointers for split files */
    split_fps = (FILE **)calloc(M, sizeof(FILE *));
    if (split_fps == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for split file pointers\n");
        fclose(input_fp);
        return -1;
    }
    
    /* Create and open M split files */
    for (i = 0; i < M; i++) {
        char filename[MAX_PATH];
        snprintf(filename, MAX_PATH, "split-%d", i);
        split_fps[i] = fopen(filename, "w");
        if (split_fps[i] == NULL) {
            fprintf(stderr, "Error: Cannot create split file '%s': %s\n", filename, strerror(errno));
            /* Cleanup already opened files */
            for (int j = 0; j < i; j++) {
                fclose(split_fps[j]);
            }
            free(split_fps);
            fclose(input_fp);
            return -1;
        }
    }
    
    /* Read input file line by line and distribute round-robin */
    while (fgets(line, sizeof(line), input_fp) != NULL) {
        /* Parse line as "source dest" */
        if (sscanf(line, "%d %d", &source, &dest) != 2) {
            fprintf(stderr, "Warning: Invalid line format at line %d, skipping: %s", line_num + 1, line);
            continue;
        }
        
        /* Skip invalid vertex numbers (negative or zero might be invalid depending on graph representation) */
        /* Assuming vertices should be >= 0 */
        if (source < 0 || dest < 0) {
            fprintf(stderr, "Warning: Negative vertex number at line %d (%d %d), skipping\n", 
                    line_num + 1, source, dest);
            continue;
        }
        
        /* Apply filtering based on MIND and MAXD */
        /* If MIND != -1 or MAXD != -1, filter by destination vertex */
        if (MIND != -1 || MAXD != -1) {
            int min_filter = (MIND != -1) ? MIND : dest;  /* No lower bound if MIND == -1 */
            int max_filter = (MAXD != -1) ? MAXD : dest;  /* No upper bound if MAXD == -1 */
            
            if (dest < min_filter || dest > max_filter) {
                /* Skip this line as it doesn't meet the filter criteria */
                line_num++;
                continue;
            }
        }
        
        /* Determine which split file to write to (round-robin) */
        int split_index = line_num % M;
        
        /* Write to the appropriate split file */
        fprintf(split_fps[split_index], "%d %d\n", source, dest);
        
        line_num++;
    }
    
    /* Check for read errors */
    if (ferror(input_fp)) {
        fprintf(stderr, "Error: Failed to read from input file\n");
        ret = -1;
    }
    
    printf("  Total lines processed: %d\n", line_num);
    
    /* Close all files */
    fclose(input_fp);
    for (i = 0; i < M; i++) {
        if (fclose(split_fps[i]) != 0) {
            fprintf(stderr, "Warning: Failed to close split-%d\n", i);
        }
    }
    free(split_fps);
    
    return ret;
}

void mapper_process(int mapper_id, int M, int R) {
    char input_filename[MAX_PATH];
    FILE *input_fp = NULL;
    FILE **intermediate_fps = NULL;
    char line[512];
    int source, dest;
    int lines_processed = 0;
    int i;
    
    printf("    [Mapper %d] Starting...\n", mapper_id);
    DEBUG_LOG(mapper_id, "Mapper starting (M=%d, R=%d)\n", M, R);
    
    /* Open input split file */
    snprintf(input_filename, MAX_PATH, "split-%d", mapper_id);
    DEBUG_LOG(mapper_id, "Opening split file: %s\n", input_filename);
    input_fp = fopen(input_filename, "r");
    if (input_fp == NULL) {
        fprintf(stderr, "    [Mapper %d] Error: Cannot open file '%s': %s\n", 
                mapper_id, input_filename, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    /* Allocate array for intermediate file pointers */
    intermediate_fps = (FILE **)calloc(R, sizeof(FILE *));
    if (intermediate_fps == NULL) {
        fprintf(stderr, "    [Mapper %d] Error: Memory allocation failed\n", mapper_id);
        fclose(input_fp);
        exit(EXIT_FAILURE);
    }
    
    /* Create and open R intermediate files: intermediate-<mapper_id>-0, ..., intermediate-<mapper_id>-(R-1) */
    DEBUG_LOG(mapper_id, "Creating %d intermediate files\n", R);
    for (i = 0; i < R; i++) {
        char filename[MAX_PATH];
        snprintf(filename, MAX_PATH, "intermediate-%d-%d", mapper_id, i);
        intermediate_fps[i] = fopen(filename, "w");
        if (intermediate_fps[i] == NULL) {
            fprintf(stderr, "    [Mapper %d] Error: Cannot create file '%s': %s\n", 
                    mapper_id, filename, strerror(errno));
            /* Cleanup already opened files */
            for (int j = 0; j < i; j++) {
                fclose(intermediate_fps[j]);
            }
            free(intermediate_fps);
            fclose(input_fp);
            exit(EXIT_FAILURE);
        }
        DEBUG_LOG(mapper_id, "Created intermediate file: %s\n", filename);
    }
    
    /* Process each line from the split file */
    while (fgets(line, sizeof(line), input_fp) != NULL) {
        /* Parse line as "source dest" */
        if (sscanf(line, "%d %d", &source, &dest) != 2) {
            fprintf(stderr, "    [Mapper %d] Warning: Invalid line format, skipping: %s", 
                    mapper_id, line);
            continue;
        }
        
        /* Calculate reducer index: k = dest % R (0-indexed) */
        int reducer_idx = dest % R;
        
        /* Write reversed pair "dest source" to intermediate-<mapper_id>-k */
        fprintf(intermediate_fps[reducer_idx], "%d %d\n", dest, source);
        
        lines_processed++;
    }
    
    /* Check for read errors */
    if (ferror(input_fp)) {
        fprintf(stderr, "    [Mapper %d] Error: Failed to read from input file\n", mapper_id);
        fclose(input_fp);
        for (i = 0; i < R; i++) {
            fclose(intermediate_fps[i]);
        }
        free(intermediate_fps);
        exit(EXIT_FAILURE);
    }
    
    printf("    [Mapper %d] Processed %d lines\n", mapper_id, lines_processed);
    DEBUG_LOG(mapper_id, "Total pairs processed: %d\n", lines_processed);
    
    /* Close all files */
    fclose(input_fp);
    DEBUG_LOG(mapper_id, "Closed input file %s\n", input_filename);
    
    for (i = 0; i < R; i++) {
        if (fclose(intermediate_fps[i]) != 0) {
            fprintf(stderr, "    [Mapper %d] Warning: Failed to close intermediate-%d-%d\n", 
                    mapper_id, mapper_id, i);
        }
    }
    DEBUG_LOG(mapper_id, "Closed all %d intermediate files\n", R);
    free(intermediate_fps);
    
    printf("    [Mapper %d] Completed successfully\n", mapper_id);
    DEBUG_LOG(mapper_id, "Exiting mapper process\n");
}

int fork_mappers(int M, int R) {
    pid_t pid;
    int i;
    int fork_failures = 0;
    
    /* Allocate array to store child PIDs (global so wait_for_children can use it) */
    child_pids = (pid_t *)calloc(M, sizeof(pid_t));
    if (child_pids == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for child PIDs\n");
        return -1;
    }
    
    /* Fork M mapper child processes */
    for (i = 0; i < M; i++) {
        pid = fork();
        
        if (pid < 0) {
            /* Fork failed */
            fprintf(stderr, "Error: fork() failed for mapper %d: %s\n", i, strerror(errno));
            fork_failures++;
            
            /* Try one more time */
            usleep(100000);  /* Wait 100ms */
            pid = fork();
            
            if (pid < 0) {
                fprintf(stderr, "Error: fork() retry failed for mapper %d: %s\n", i, strerror(errno));
                /* Clean up: wait for already forked children */
                for (int j = 0; j < i; j++) {
                    if (child_pids[j] > 0) {
                        waitpid(child_pids[j], NULL, 0);
                    }
                }
                free(child_pids);
                child_pids = NULL;
                return -1;
            }
        }
        
        if (pid == 0) {
            /* Child process */
            free(child_pids);  /* Child doesn't need the PID array */
            child_pids = NULL;
            mapper_process(i, M, R);
            exit(EXIT_SUCCESS);
        } else {
            /* Parent process - store child PID */
            child_pids[i] = pid;
            printf("  Forked mapper %d (PID: %d)\n", i, pid);
        }
    }
    
    if (fork_failures > 0) {
        fprintf(stderr, "Warning: %d fork failure(s) occurred but were retried\n", fork_failures);
    }
    
    return 0;
}

void* create_shared_memory_region(int SHMSIZE, int R) {
    int fd;
    void *ptr;
    size_t size;
    
    /* Calculate size: 1ULL << SHMSIZE (handle large values safely) */
    if (SHMSIZE < 0 || SHMSIZE > 40) {
        fprintf(stderr, "Error: SHMSIZE must be in range [0, 40] (got %d)\n", SHMSIZE);
        return NULL;
    }
    size = 1ULL << SHMSIZE;
    
    /* Create unique shared memory name using PID */
    snprintf(shm_name, MAX_PATH, "%s%d", SHM_NAME_PREFIX, getpid());
    
    /* Create POSIX shared memory object */
    fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        if (errno == EEXIST) {
            /* Try to unlink and recreate */
            fprintf(stderr, "Warning: Shared memory %s already exists, unlinking...\n", shm_name);
            shm_unlink(shm_name);
            fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
        }
        
        if (fd < 0) {
            fprintf(stderr, "Error: shm_open() failed for %s: %s\n", shm_name, strerror(errno));
            return NULL;
        }
    }
    
    /* Set the size of the shared memory object */
    if (ftruncate(fd, size) < 0) {
        fprintf(stderr, "Error: ftruncate() failed: %s\n", strerror(errno));
        close(fd);
        shm_unlink(shm_name);
        return NULL;
    }
    
    /* Map the shared memory into the address space */
    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "Error: mmap() failed: %s\n", strerror(errno));
        close(fd);
        shm_unlink(shm_name);
        return NULL;
    }
    
    /* Initialize memory to zero */
    memset(ptr, 0, size);
    
    /* Store globally */
    shm_fd = fd;
    shm_ptr = ptr;
    shm_size = size;
    
    printf("  Created shared memory: name=%s, size=%zu, fd=%d\n", shm_name, size, fd);
    
    return ptr;
}

void initialize_shm_layout(void *shm_ptr, int R, size_t total_size) {
    uint32_t *r_field;
    uint64_t *offsets;
    size_t header_size;
    size_t data_size;
    size_t region_size;
    int i;
    
    if (shm_ptr == NULL) {
        fprintf(stderr, "Error: NULL shared memory pointer\n");
        return;
    }
    
    /* Layout:
     * [0-3]:       uint32_t R (number of reducers)
     * [4-4+8*R+7]: uint64_t offsets[R+1] (offset for each reducer + end marker)
     * [remaining]: data regions for each reducer
     */
    
    /* Calculate header size */
    header_size = sizeof(uint32_t) + (R + 1) * sizeof(uint64_t);
    data_size = total_size - header_size;
    region_size = data_size / R;
    
    printf("  SHM Layout: header=%zu bytes, data=%zu bytes, region_size=%zu bytes/reducer\n",
           header_size, data_size, region_size);
    
    /* Write R value */
    r_field = (uint32_t *)shm_ptr;
    *r_field = (uint32_t)R;
    
    /* Write offsets for each reducer */
    offsets = (uint64_t *)((char *)shm_ptr + sizeof(uint32_t));
    for (i = 0; i <= R; i++) {
        offsets[i] = header_size + (i * region_size);
    }
    
    printf("  Initialized SHM layout: R=%d, offsets[0]=%lu, offsets[%d]=%lu\n",
           R, (unsigned long)offsets[0], R, (unsigned long)offsets[R]);
}

void cleanup_shared_memory(void *ptr, size_t size, const char *name) {
    if (ptr != NULL && ptr != MAP_FAILED) {
        if (munmap(ptr, size) < 0) {
            fprintf(stderr, "Warning: munmap() failed: %s\n", strerror(errno));
        } else {
            printf("  Unmapped shared memory (%zu bytes)\n", size);
        }
    }
    
    if (shm_fd >= 0) {
        close(shm_fd);
        shm_fd = -1;
    }
    
    if (name != NULL && name[0] != '\0') {
        if (shm_unlink(name) < 0) {
            fprintf(stderr, "Warning: shm_unlink(%s) failed: %s\n", name, strerror(errno));
        } else {
            printf("  Unlinked shared memory: %s\n", name);
        }
    }
}

int create_shared_memory(int shmsize, int R) {
    void *ptr;
    
    /* Create the shared memory region */
    ptr = create_shared_memory_region(shmsize, R);
    if (ptr == NULL) {
        return -1;
    }
    
    /* Initialize the layout */
    initialize_shm_layout(ptr, R, shm_size);
    
    return 0;
}

/* Helper functions for reducer */

/**
 * Initialize a PairArray
 */
void init_pair_array(PairArray *arr) {
    arr->pairs = (Pair *)malloc(INITIAL_PAIR_CAPACITY * sizeof(Pair));
    arr->count = 0;
    arr->capacity = INITIAL_PAIR_CAPACITY;
}

/**
 * Add a pair to the array, expanding if necessary
 */
int add_pair(PairArray *arr, uint32_t dest, uint32_t source) {
    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity * 2;
        Pair *new_pairs = (Pair *)realloc(arr->pairs, new_capacity * sizeof(Pair));
        if (new_pairs == NULL) {
            fprintf(stderr, "Error: Failed to expand pair array\n");
            return -1;
        }
        arr->pairs = new_pairs;
        arr->capacity = new_capacity;
    }
    
    arr->pairs[arr->count].dest = dest;
    arr->pairs[arr->count].source = source;
    arr->count++;
    return 0;
}

/**
 * Free a PairArray
 */
void free_pair_array(PairArray *arr) {
    if (arr->pairs != NULL) {
        free(arr->pairs);
        arr->pairs = NULL;
    }
    arr->count = 0;
    arr->capacity = 0;
}

/**
 * Comparator for qsort: sort by dest (primary), then by source (secondary)
 */
int compare_pairs(const void *a, const void *b) {
    const Pair *p1 = (const Pair *)a;
    const Pair *p2 = (const Pair *)b;
    
    if (p1->dest < p2->dest) return -1;
    if (p1->dest > p2->dest) return 1;
    
    /* dest is equal, compare source */
    if (p1->source < p2->source) return -1;
    if (p1->source > p2->source) return 1;
    
    return 0;
}

/**
 * Write (dest, count) pair to shared memory region
 * Returns number of bytes written, or -1 on overflow
 */
int write_to_shared_memory(void *shm_ptr, int reducer_id, uint32_t dest, size_t count,
                           size_t *written_bytes, size_t max_size) {
    if (shm_ptr == NULL) {
        return 0;  /* No SHM, skip */
    }
    
    /* Get reducer's region */
    uint64_t *offsets = (uint64_t *)((char *)shm_ptr + sizeof(uint32_t));
    size_t my_offset = offsets[reducer_id];
    char *my_region = (char *)shm_ptr + my_offset;
    
    /* Format line: "dest count\n" */
    char shm_line[64];
    int line_len = snprintf(shm_line, sizeof(shm_line), "%u %zu\n", dest, count);
    
    /* Check bounds */
    if (line_len > 0 && *written_bytes + line_len <= max_size) {
        memcpy(my_region + *written_bytes, shm_line, line_len);
        *written_bytes += line_len;
        return line_len;
    } else if (*written_bytes + line_len > max_size) {
        fprintf(stderr, "    [Reducer %d] Warning: SHM overflow, skipping dest %u\n",
                reducer_id, dest);
        return -1;  /* Overflow */
    }
    
    return 0;
}

/**
 * Group sorted pairs by destination, remove duplicates, and write output
 * Returns number of destinations written
 */
int group_and_write_output(Pair *pairs, size_t pair_count, FILE *output_fp,
                           void *shm_ptr, int reducer_id, size_t shm_max_size) {
    size_t idx = 0;
    int dest_count = 0;
    size_t shm_written = 0;
    
    while (idx < pair_count) {
        uint32_t current_dest = pairs[idx].dest;
        uint32_t *sources = NULL;
        size_t source_capacity = 16;
        size_t source_count = 0;
        
        /* Allocate array for unique sources for this destination */
        sources = (uint32_t *)malloc(source_capacity * sizeof(uint32_t));
        if (sources == NULL) {
            fprintf(stderr, "    [Reducer %d] Error: Memory allocation failed for sources\n", 
                    reducer_id);
            return -1;
        }
        
        /* Collect all unique sources for this destination */
        uint32_t prev_source = UINT32_MAX;  /* Sentinel value */
        
        while (idx < pair_count && pairs[idx].dest == current_dest) {
            uint32_t src = pairs[idx].source;
            
            /* Since pairs are sorted by source within same dest,
             * duplicates are consecutive - just check if different from previous */
            if (src != prev_source) {
                /* Expand array if needed */
                if (source_count >= source_capacity) {
                    source_capacity *= 2;
                    uint32_t *new_sources = (uint32_t *)realloc(sources, 
                                                                source_capacity * sizeof(uint32_t));
                    if (new_sources == NULL) {
                        fprintf(stderr, "    [Reducer %d] Error: Failed to expand source array\n",
                                reducer_id);
                        free(sources);
                        return -1;
                    }
                    sources = new_sources;
                }
                
                /* Add unique source */
                sources[source_count++] = src;
                prev_source = src;
            }
            
            idx++;
        }
        
        /* Write to output file: "dest: s1 s2 s3\n" */
        fprintf(output_fp, "%u:", current_dest);
        for (size_t j = 0; j < source_count; j++) {
            fprintf(output_fp, " %u", sources[j]);
        }
        fprintf(output_fp, "\n");
        
        /* Write to shared memory: "dest count\n" */
        write_to_shared_memory(shm_ptr, reducer_id, current_dest, source_count,
                              &shm_written, shm_max_size);
        
        free(sources);
        dest_count++;
    }
    
    printf("    [Reducer %d] Wrote %d destinations, %zu bytes to SHM\n", 
           reducer_id, dest_count, shm_written);
    
    return dest_count;
}

void reducer_process(int reducer_id, int M, int R, void *shm_ptr) {
    PairArray pairs;
    FILE *output_fp = NULL;
    char output_filename[MAX_PATH];
    int i;
    uint64_t *offsets;
    size_t my_offset, my_size;
    
    printf("    [Reducer %d] Starting...\n", reducer_id);
    
    /* Initialize pair array */
    init_pair_array(&pairs);
    
    /* Step 1: Read all intermediate-i-<reducer_id> files from all M mappers */
    for (i = 0; i < M; i++) {
        char filename[MAX_PATH];
        FILE *fp;
        char line[512];
        uint32_t dest, source;
        
        snprintf(filename, MAX_PATH, "intermediate-%d-%d", i, reducer_id);
        fp = fopen(filename, "r");
        
        if (fp == NULL) {
            /* File may not exist if mapper had no data for this reducer */
            if (errno == ENOENT) {
                continue;  /* Skip this file */
            } else {
                fprintf(stderr, "    [Reducer %d] Error: Cannot open file '%s': %s\n",
                        reducer_id, filename, strerror(errno));
                free_pair_array(&pairs);
                exit(EXIT_FAILURE);
            }
        }
        
        /* Read all lines from this intermediate file */
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (sscanf(line, "%u %u", &dest, &source) != 2) {
                fprintf(stderr, "    [Reducer %d] Warning: Invalid line in %s: %s",
                        reducer_id, filename, line);
                continue;
            }
            
            if (add_pair(&pairs, dest, source) != 0) {
                fprintf(stderr, "    [Reducer %d] Error: Failed to add pair\n", reducer_id);
                fclose(fp);
                free_pair_array(&pairs);
                exit(EXIT_FAILURE);
            }
        }
        
        fclose(fp);
    }
    
    printf("    [Reducer %d] Read %zu pairs from %d mappers\n", reducer_id, pairs.count, M);
    
    /* Step 2: Sort pairs by dest (primary), source (secondary) using qsort */
    if (pairs.count > 0) {
        qsort(pairs.pairs, pairs.count, sizeof(Pair), compare_pairs);
        printf("    [Reducer %d] Sorted %zu pairs\n", reducer_id, pairs.count);
    }
    
    /* Step 3: Open output file */
    snprintf(output_filename, MAX_PATH, "output-%d", reducer_id);
    output_fp = fopen(output_filename, "w");
    if (output_fp == NULL) {
        fprintf(stderr, "    [Reducer %d] Error: Cannot create output file '%s': %s\n",
                reducer_id, output_filename, strerror(errno));
        free_pair_array(&pairs);
        exit(EXIT_FAILURE);
    }
    
    /* Step 4: Get shared memory region size for this reducer */
    if (shm_ptr != NULL) {
        offsets = (uint64_t *)((char *)shm_ptr + sizeof(uint32_t));
        my_offset = offsets[reducer_id];
        my_size = offsets[reducer_id + 1] - my_offset;
        
        printf("    [Reducer %d] SHM region: offset=%zu, size=%zu\n", 
               reducer_id, my_offset, my_size);
    } else {
        my_size = 0;
    }
    
    /* Step 5: Group by destination, remove duplicates, and write output
     * This function handles:
     * - Grouping sorted pairs by destination
     * - Removing duplicate sources (via consecutive comparison)
     * - Writing to output file in format "dest: s1 s2 s3\n"
     * - Writing to shared memory in format "dest count\n"
     */
    int dest_count = group_and_write_output(pairs.pairs, pairs.count, output_fp,
                                           shm_ptr, reducer_id, my_size);
    
    if (dest_count < 0) {
        fprintf(stderr, "    [Reducer %d] Error: Failed to group and write output\n", reducer_id);
        fclose(output_fp);
        free_pair_array(&pairs);
        exit(EXIT_FAILURE);
    }
    
    /* Cleanup */
    fclose(output_fp);
    free_pair_array(&pairs);
    
    printf("    [Reducer %d] Completed successfully\n", reducer_id);
}

int fork_reducers(int R, int M) {
    pid_t pid;
    int i;
    int fork_failures = 0;
    
    /* Allocate array to store child PIDs (reusing global child_pids) */
    child_pids = (pid_t *)calloc(R, sizeof(pid_t));
    if (child_pids == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for reducer PIDs\n");
        return -1;
    }
    
    /* Fork R reducer child processes */
    for (i = 0; i < R; i++) {
        pid = fork();
        
        if (pid < 0) {
            /* Fork failed */
            fprintf(stderr, "Error: fork() failed for reducer %d: %s\n", i, strerror(errno));
            fork_failures++;
            
            /* Try one more time */
            usleep(100000);  /* Wait 100ms */
            pid = fork();
            
            if (pid < 0) {
                fprintf(stderr, "Error: fork() retry failed for reducer %d: %s\n", i, strerror(errno));
                /* Clean up: wait for already forked children */
                for (int j = 0; j < i; j++) {
                    if (child_pids[j] > 0) {
                        waitpid(child_pids[j], NULL, 0);
                    }
                }
                free(child_pids);
                child_pids = NULL;
                return -1;
            }
        }
        
        if (pid == 0) {
            /* Child process */
            free(child_pids);
            child_pids = NULL;
            reducer_process(i, M, R, shm_ptr);
            exit(EXIT_SUCCESS);
        } else {
            /* Parent process - store child PID */
            child_pids[i] = pid;
            printf("  Forked reducer %d (PID: %d)\n", i, pid);
        }
    }
    
    if (fork_failures > 0) {
        fprintf(stderr, "Warning: %d fork failure(s) occurred but were retried\n", fork_failures);
    }
    
    return 0;
}

/* Structure to track each output file reader during R-way merge */
typedef struct {
    FILE *fp;
    char current_line[256];
    int dest;
    int eof;
    int file_id;
} FileReader;

/**
 * Parse destination from line format "dest: source1 source2 ...\n"
 * Returns destination value, or -1 on parse error
 */
int parse_destination(const char *line) {
    int dest;
    if (sscanf(line, "%d:", &dest) == 1) {
        return dest;
    }
    return -1;
}

/**
 * Read next line from a file reader and update its state
 * Returns 1 if line read, 0 if EOF, -1 on error
 */
int read_next_line(FileReader *reader) {
    if (reader->eof) {
        return 0;
    }
    
    if (fgets(reader->current_line, sizeof(reader->current_line), reader->fp) != NULL) {
        reader->dest = parse_destination(reader->current_line);
        if (reader->dest < 0) {
            fprintf(stderr, "Warning: Invalid line format in output-%d: %s", 
                    reader->file_id, reader->current_line);
            return -1;
        }
        return 1;
    } else {
        reader->eof = 1;
        return 0;
    }
}

/**
 * R-way merge of output-0, output-1, ..., output-(R-1) into OUT1
 * Output is sorted by destination (ascending)
 */
int merge_output_files(int R, const char *out1) {
    FileReader *readers = NULL;
    FILE *out1_fp = NULL;
    int i;
    int active_readers = 0;
    int lines_written = 0;
    
    /* Allocate array of file readers */
    readers = (FileReader *)calloc(R, sizeof(FileReader));
    if (readers == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for file readers\n");
        return -1;
    }
    
    /* Open all R output files */
    for (i = 0; i < R; i++) {
        char filename[MAX_PATH];
        snprintf(filename, MAX_PATH, "output-%d", i);
        
        readers[i].fp = fopen(filename, "r");
        readers[i].file_id = i;
        readers[i].eof = 0;
        readers[i].dest = -1;
        readers[i].current_line[0] = '\0';
        
        if (readers[i].fp == NULL) {
            if (errno == ENOENT) {
                /* File doesn't exist - reducer had no data, mark as EOF */
                readers[i].eof = 1;
                readers[i].fp = NULL;
            } else {
                fprintf(stderr, "Error: Cannot open output file '%s': %s\n", 
                        filename, strerror(errno));
                /* Cleanup opened files */
                for (int j = 0; j < i; j++) {
                    if (readers[j].fp != NULL) {
                        fclose(readers[j].fp);
                    }
                }
                free(readers);
                return -1;
            }
        } else {
            /* Read first line from this file */
            if (read_next_line(&readers[i]) > 0) {
                active_readers++;
            }
        }
    }
    
    /* Open output file OUT1 */
    out1_fp = fopen(out1, "w");
    if (out1_fp == NULL) {
        fprintf(stderr, "Error: Cannot create output file '%s': %s\n", 
                out1, strerror(errno));
        for (i = 0; i < R; i++) {
            if (readers[i].fp != NULL) {
                fclose(readers[i].fp);
            }
        }
        free(readers);
        return -1;
    }
    
    printf("  Merging %d output files (active: %d)...\n", R, active_readers);
    
    /* R-way merge: repeatedly find minimum destination and write */
    while (active_readers > 0) {
        int min_idx = -1;
        int min_dest = INT_MAX;
        
        /* Find reader with smallest destination */
        for (i = 0; i < R; i++) {
            if (!readers[i].eof && readers[i].dest < min_dest) {
                min_dest = readers[i].dest;
                min_idx = i;
            }
        }
        
        if (min_idx < 0) {
            /* No more data (shouldn't happen if active_readers > 0) */
            break;
        }
        
        /* Write the line with minimum destination to OUT1 */
        fputs(readers[min_idx].current_line, out1_fp);
        lines_written++;
        
        /* Read next line from that file */
        if (read_next_line(&readers[min_idx]) <= 0) {
            active_readers--;
        }
    }
    
    printf("  Wrote %d lines to %s\n", lines_written, out1);
    
    /* Close all files */
    fclose(out1_fp);
    for (i = 0; i < R; i++) {
        if (readers[i].fp != NULL) {
            fclose(readers[i].fp);
        }
    }
    free(readers);
    
    return 0;
}

/**
 * Initialize a DestCountArray
 */
void init_destcount_array(DestCountArray *arr) {
    arr->entries = (DestCount *)malloc(INITIAL_DESTCOUNT_CAPACITY * sizeof(DestCount));
    arr->count = 0;
    arr->capacity = INITIAL_DESTCOUNT_CAPACITY;
}

/**
 * Add a (dest, count) entry to the array, expanding if necessary
 */
int add_destcount(DestCountArray *arr, uint32_t dest, uint32_t count) {
    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity * 2;
        DestCount *new_entries = (DestCount *)realloc(arr->entries, new_capacity * sizeof(DestCount));
        if (new_entries == NULL) {
            fprintf(stderr, "Error: Failed to expand destcount array\n");
            return -1;
        }
        arr->entries = new_entries;
        arr->capacity = new_capacity;
    }
    
    arr->entries[arr->count].dest = dest;
    arr->entries[arr->count].count = count;
    arr->count++;
    return 0;
}

/**
 * Free a DestCountArray
 */
void free_destcount_array(DestCountArray *arr) {
    if (arr->entries != NULL) {
        free(arr->entries);
        arr->entries = NULL;
    }
    arr->count = 0;
    arr->capacity = 0;
}

/**
 * Comparator for qsort: sort DestCount by dest (ascending)
 */
int compare_destcount(const void *a, const void *b) {
    const DestCount *dc1 = (const DestCount *)a;
    const DestCount *dc2 = (const DestCount *)b;
    
    if (dc1->dest < dc2->dest) return -1;
    if (dc1->dest > dc2->dest) return 1;
    return 0;
}

/**
 * Read shared memory, parse (dest, count) pairs, sort, and write to OUT2
 */
int process_shared_memory(const char *out2) {
    DestCountArray dc_array;
    FILE *out2_fp = NULL;
    uint32_t *r_value;
    uint64_t *offsets;
    int R;
    int i;
    
    if (shm_ptr == NULL) {
        fprintf(stderr, "Error: Shared memory pointer is NULL\n");
        return -1;
    }
    
    /* Read header: R value and offsets */
    r_value = (uint32_t *)shm_ptr;
    R = (int)(*r_value);
    offsets = (uint64_t *)((char *)shm_ptr + sizeof(uint32_t));
    
    printf("  Reading data from %d reducer regions in SHM\n", R);
    
    /* Initialize dynamic array for (dest, count) pairs */
    init_destcount_array(&dc_array);
    
    /* Read each reducer's region */
    for (i = 0; i < R; i++) {
        size_t region_start = offsets[i];
        size_t region_end = offsets[i + 1];
        size_t region_size = region_end - region_start;
        char *region = (char *)shm_ptr + region_start;
        
        /* Find actual data length (up to first null byte or region end) */
        size_t data_len = 0;
        for (size_t j = 0; j < region_size && region[j] != '\0'; j++) {
            data_len++;
        }
        
        if (data_len == 0) {
            /* Empty region - reducer had no data */
            continue;
        }
        
        /* Parse lines from this region: "dest count\n" */
        char *line_start = region;
        char *line_end;
        
        while (line_start < region + data_len) {
            /* Find end of line */
            line_end = line_start;
            while (line_end < region + data_len && *line_end != '\n') {
                line_end++;
            }
            
            if (line_end > line_start) {
                /* Parse "dest count" */
                uint32_t dest, count;
                char temp_line[256];
                size_t line_len = line_end - line_start;
                
                if (line_len >= sizeof(temp_line)) {
                    line_len = sizeof(temp_line) - 1;
                }
                
                memcpy(temp_line, line_start, line_len);
                temp_line[line_len] = '\0';
                
                if (sscanf(temp_line, "%u %u", &dest, &count) == 2) {
                    if (add_destcount(&dc_array, dest, count) != 0) {
                        fprintf(stderr, "Error: Failed to add dest-count pair\n");
                        free_destcount_array(&dc_array);
                        return -1;
                    }
                } else {
                    fprintf(stderr, "Warning: Invalid format in reducer %d region: %s\n", 
                            i, temp_line);
                }
            }
            
            /* Move to next line */
            line_start = line_end + 1;
        }
    }
    
    printf("  Read %zu (dest, count) pairs from SHM\n", dc_array.count);
    
    /* Sort by destination */
    if (dc_array.count > 0) {
        qsort(dc_array.entries, dc_array.count, sizeof(DestCount), compare_destcount);
        printf("  Sorted %zu entries by destination\n", dc_array.count);
    }
    
    /* Write to OUT2 */
    out2_fp = fopen(out2, "w");
    if (out2_fp == NULL) {
        fprintf(stderr, "Error: Cannot create output file '%s': %s\n", 
                out2, strerror(errno));
        free_destcount_array(&dc_array);
        return -1;
    }
    
    for (i = 0; i < (int)dc_array.count; i++) {
        fprintf(out2_fp, "%u %u\n", dc_array.entries[i].dest, dc_array.entries[i].count);
    }
    
    printf("  Wrote %zu lines to %s\n", dc_array.count, out2);
    
    /* Cleanup */
    fclose(out2_fp);
    free_destcount_array(&dc_array);
    
    return 0;
}

void cleanup(int M, int R) {
    int i;
    
    /* Cleanup shared memory */
    if (shm_ptr != NULL) {
        cleanup_shared_memory(shm_ptr, shm_size, shm_name);
        shm_ptr = NULL;
    }
    
    /* Remove temporary split files */
    for (i = 0; i < M; i++) {
        char filename[MAX_PATH];
        snprintf(filename, MAX_PATH, "split-%d", i);
        if (remove(filename) != 0 && errno != ENOENT) {
            fprintf(stderr, "Warning: Failed to remove %s: %s\n", filename, strerror(errno));
        }
    }
    
    /* Remove temporary intermediate files */
    for (i = 0; i < M; i++) {
        for (int j = 0; j < R; j++) {
            char filename[MAX_PATH];
            snprintf(filename, MAX_PATH, "intermediate-%d-%d", i, j);
            if (remove(filename) != 0 && errno != ENOENT) {
                fprintf(stderr, "Warning: Failed to remove %s: %s\n", filename, strerror(errno));
            }
        }
    }
    
    /* Remove temporary output files (created by R reducers) */
    for (i = 0; i < R; i++) {
        char filename[MAX_PATH];
        snprintf(filename, MAX_PATH, "output-%d", i);
        if (remove(filename) != 0 && errno != ENOENT) {
            fprintf(stderr, "Warning: Failed to remove %s: %s\n", filename, strerror(errno));
        }
    }
}

int wait_for_children(int num_children) {
    int failed_children = 0;
    int i;
    
    if (child_pids == NULL) {
        fprintf(stderr, "Error: No child PIDs to wait for\n");
        return -1;
    }
    
    /* Wait for all children */
    for (i = 0; i < num_children; i++) {
        if (child_pids[i] > 0) {
            int status;
            pid_t result = waitpid(child_pids[i], &status, 0);
            
            if (result < 0) {
                fprintf(stderr, "Error: waitpid() failed for child %d (PID %d): %s\n", 
                        i, child_pids[i], strerror(errno));
                failed_children++;
            } else if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0) {
                    fprintf(stderr, "Error: Child %d (PID %d) exited with code %d\n", 
                            i, child_pids[i], exit_code);
                    failed_children++;
                } else {
                    printf("  Child %d (PID %d) completed successfully\n", i, child_pids[i]);
                }
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "Error: Child %d (PID %d) terminated by signal %d\n", 
                        i, child_pids[i], WTERMSIG(status));
                failed_children++;
            }
        }
    }
    
    /* Free the child_pids array after waiting */
    free(child_pids);
    child_pids = NULL;
    
    if (failed_children > 0) {
        fprintf(stderr, "Error: %d child process(es) failed\n", failed_children);
        return -1;
    }
    
    return 0;
}


#ifdef TEST_MODE
/*
 * Test function for split_input_file()
 * Compile with: gcc -DTEST_MODE -Wall -pthread -o test_findsp findsp.c -lm
 */
void test_split() {
    const char *test_file = "test_input.txt";
    const int M = 4;
    FILE *fp;
    int i;
    
    printf("\n=== TEST: split_input_file() ===\n\n");
    
    /* Step 1: Create a test input file with 20 lines */
    printf("[1] Creating test input file '%s' with 20 lines...\n", test_file);
    fp = fopen(test_file, "w");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot create test file\n");
        return;
    }
    
    /* Write 20 lines with various patterns */
    fprintf(fp, "0 1\n");    // Line 0 -> split-0
    fprintf(fp, "1 2\n");    // Line 1 -> split-1
    fprintf(fp, "2 3\n");    // Line 2 -> split-2
    fprintf(fp, "3 4\n");    // Line 3 -> split-3
    fprintf(fp, "4 5\n");    // Line 4 -> split-0
    fprintf(fp, "5 6\n");    // Line 5 -> split-1
    fprintf(fp, "6 7\n");    // Line 6 -> split-2
    fprintf(fp, "7 8\n");    // Line 7 -> split-3
    fprintf(fp, "8 9\n");    // Line 8 -> split-0
    fprintf(fp, "9 10\n");   // Line 9 -> split-1
    fprintf(fp, "10 11\n");  // Line 10 -> split-2
    fprintf(fp, "11 12\n");  // Line 11 -> split-3
    fprintf(fp, "12 12\n");  // Line 12 -> split-0 (self-loop)
    fprintf(fp, "13 14\n");  // Line 13 -> split-1
    fprintf(fp, "14 15\n");  // Line 14 -> split-2
    fprintf(fp, "15 16\n");  // Line 15 -> split-3
    fprintf(fp, "1 2\n");    // Line 16 -> split-0 (duplicate)
    fprintf(fp, "17 18\n");  // Line 17 -> split-1
    fprintf(fp, "18 19\n");  // Line 18 -> split-2
    fprintf(fp, "19 20\n");  // Line 19 -> split-3
    fclose(fp);
    printf("  Test file created successfully\n");
    
    /* Step 2: Call split_input_file with M=4, no filtering */
    printf("\n[2] Calling split_input_file() with M=%d, MIND=-1, MAXD=-1...\n", M);
    if (split_input_file(test_file, M, -1, -1) != 0) {
        fprintf(stderr, "Error: split_input_file() failed\n");
        return;
    }
    printf("  split_input_file() completed successfully\n");
    
    /* Step 3: Verify each split file has ~5 lines */
    printf("\n[3] Verifying split files...\n");
    int total_lines = 0;
    for (i = 0; i < M; i++) {
        char filename[MAX_PATH];
        char line[512];
        int line_count = 0;
        
        snprintf(filename, MAX_PATH, "split-%d", i);
        fp = fopen(filename, "r");
        if (fp == NULL) {
            fprintf(stderr, "Error: Cannot open '%s'\n", filename);
            continue;
        }
        
        printf("  %s contents:\n", filename);
        while (fgets(line, sizeof(line), fp) != NULL) {
            printf("    %s", line);
            line_count++;
        }
        fclose(fp);
        
        printf("  %s has %d lines\n", filename, line_count);
        total_lines += line_count;
    }
    
    /* Step 4: Verify round-robin distribution */
    printf("\n[4] Verifying round-robin distribution:\n");
    printf("  Expected: Each split file should have 5 lines (20 lines / 4 splits)\n");
    printf("  Actual total: %d lines\n", total_lines);
    if (total_lines == 20) {
        printf("  ✓ Round-robin distribution is correct!\n");
    } else {
        printf("  ✗ Distribution error: expected 20 lines total, got %d\n", total_lines);
    }
    
    /* Step 5: Test with filtering */
    printf("\n[5] Testing with MIND=5, MAXD=15...\n");
    if (split_input_file(test_file, M, 5, 15) != 0) {
        fprintf(stderr, "Error: split_input_file() with filtering failed\n");
        return;
    }
    
    printf("\n  Verifying filtered split files:\n");
    total_lines = 0;
    for (i = 0; i < M; i++) {
        char filename[MAX_PATH];
        char line[512];
        int line_count = 0;
        
        snprintf(filename, MAX_PATH, "split-%d", i);
        fp = fopen(filename, "r");
        if (fp == NULL) {
            fprintf(stderr, "Error: Cannot open '%s'\n", filename);
            continue;
        }
        
        while (fgets(line, sizeof(line), fp) != NULL) {
            line_count++;
        }
        fclose(fp);
        
        printf("  %s has %d lines (filtered)\n", filename, line_count);
        total_lines += line_count;
    }
    printf("  Total filtered lines: %d (should be less than 20)\n", total_lines);
    
    printf("\n=== TEST COMPLETE ===\n");
}

/* Test main function */
int main(void) {
    test_split();
    
    /* Cleanup test files */
    printf("\nCleaning up test files...\n");
    remove("test_input.txt");
    remove("split-0");
    remove("split-1");
    remove("split-2");
    remove("split-3");
    printf("Cleanup complete.\n");
    
    return 0;
}
#endif /* TEST_MODE */

