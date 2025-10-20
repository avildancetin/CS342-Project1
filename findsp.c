#include <stdio.h>    // For standard I/O functions (printf, fopen, etc.)
#include <stdlib.h>   // For general utilities (atoi, exit, malloc, etc.)
#include <string.h>   // For string manipulation (strcpy, etc.)
#include <unistd.h>   // For POSIX functions (fork, wait, etc.)
#include <sys/wait.h> // For wait/waitpid
#include <fcntl.h>    // For file control options (O_CREAT, etc.) - Needed later for shm
#include <errno.h>    // For error number variables (like errno)
#include <math.h>     // For pow() function
#include <sys/mman.h> // For mmap, munmap, shm_open, shm_unlink
#include <signal.h>   // For SIGTERM signal
#include <limits.h>   // For LONG_MAX

// Define constants for argument indices for clarity
#define ARG_INFILE 1
#define ARG_M 2
#define ARG_R 3
#define ARG_OUT1 4
#define ARG_OUT2 5
#define ARG_MIND 6
#define ARG_MAXD 7
#define ARG_SHMSIZE 8
#define EXPECTED_ARGS 9 // Program name + 8 arguments

// Data structures
typedef struct {
    long dest;
    long src;
} DestSrcPair;

typedef struct {
    long dest;
    int count;
} DestCountPair;

// Function declarations
void run_mapper(int mapper_id, int R, long mind, long maxd);
void run_reducer(int reducer_id, int M, int R, const char *shm_name, size_t shm_segment_size);
int compare_dest_src(const void *a, const void *b);
int compare_dest_count(const void *a, const void *b);
int compare_long(const void *a, const void *b);

int main(int argc, char *argv[])
{
    // --- Argument Parsing ---
    if (argc != EXPECTED_ARGS)
    {
        fprintf(stderr, "Usage: %s INFILE M R OUT1 OUT2 MIND MAXD SHMSIZE\n", argv[0]);
        fprintf(stderr, "Error: Expected %d arguments, but got %d\n", EXPECTED_ARGS - 1, argc - 1);
        exit(EXIT_FAILURE); // Use EXIT_FAILURE for errors
    }

    // Store arguments in variables
    char *infile_name = argv[ARG_INFILE];
    int M = atoi(argv[ARG_M]); // Convert string argument to integer
    int R = atoi(argv[ARG_R]);
    char *out1_name = argv[ARG_OUT1];
    char *out2_name = argv[ARG_OUT2];
    long mind = atol(argv[ARG_MIND]); // Use atol for potentially large vertex numbers if needed, though int might suffice
    long maxd = atol(argv[ARG_MAXD]);
    int shmsize_exponent = atoi(argv[ARG_SHMSIZE]);

    // --- Input Validation ---
    if (M <= 0 || M > 20)
    {
        fprintf(stderr, "Error: M (number of mappers) must be between 1 and 20.\n");
        exit(EXIT_FAILURE);
    }
    if (R <= 0 || R > 10)
    {
        fprintf(stderr, "Error: R (number of reducers) must be between 1 and 10.\n");
        exit(EXIT_FAILURE);
    }
    if (shmsize_exponent < 0)
    { // Basic check, could add upper limit
        fprintf(stderr, "Error: SHMSIZE exponent cannot be negative.\n");
        exit(EXIT_FAILURE);
    }

    // Validate MIND and MAXD (only if not -1)
    if (mind != -1 && mind <= 0)
    {
        fprintf(stderr, "Error: MIND must be positive or -1.\n");
        exit(EXIT_FAILURE);
    }
    if (maxd != -1 && maxd <= 0)
    {
        fprintf(stderr, "Error: MAXD must be positive or -1.\n");
        exit(EXIT_FAILURE);
    }
    if (mind != -1 && maxd != -1 && mind > maxd)
    {
        fprintf(stderr, "Error: MIND cannot be greater than MAXD.\n");
        exit(EXIT_FAILURE);
    }

    printf("Arguments Parsed:\n");
    printf("  INFILE: %s\n", infile_name);
    printf("  M: %d\n", M);
    printf("  R: %d\n", R);
    printf("  OUT1: %s\n", out1_name);
    printf("  OUT2: %s\n", out2_name);
    printf("  MIND: %ld\n", mind);
    printf("  MAXD: %ld\n", maxd);
    printf("  SHMSIZE (exponent): %d\n", shmsize_exponent);
    printf("------------------------------------\n");

// --- Step 1: Splitting the Input File ---
    printf("Step 1: Splitting the input file '%s' into %d parts...\n", infile_name, M);

    FILE *infile = fopen(infile_name, "r");
    if (!infile) {
        perror("Error opening input file");
        exit(EXIT_FAILURE);
    }

    // Create and open M split files
    FILE *split_files[M];
    char split_filename[20]; // Buffer for split file names (e.g., "split-10")
    for (int i = 0; i < M; ++i) {
        sprintf(split_filename, "split-%d", i + 1);
        split_files[i] = fopen(split_filename, "w");
        if (!split_files[i]) {
            perror("Error creating split file");
            // Close already opened files before exiting
            for (int k = 0; k < i; ++k) {
                fclose(split_files[k]);
            }
            fclose(infile);
            exit(EXIT_FAILURE);
        }
    }

    // Read infile line by line and distribute to split files
    char *line = NULL;        // Buffer for getline
    size_t len = 0;           // Size for getline
    ssize_t read_len;         // Return value for getline
    int current_split_file_index = 0;

    while ((read_len = getline(&line, &len, infile)) != -1) {
        // Write the line to the current split file
        fprintf(split_files[current_split_file_index], "%s", line);

        // Move to the next split file index (round-robin)
        current_split_file_index = (current_split_file_index + 1) % M;
    }

    // Clean up: Close all files and free getline buffer
    fclose(infile);
    for (int i = 0; i < M; ++i) {
        fclose(split_files[i]);
    }
    if (line) {
        free(line); // Free memory allocated by getline
    }
    printf("Step 1: Input file splitting complete.\n");
    printf("------------------------------------\n");

   // --- Step 2: Create Mapper Processes ---
    printf("Step 2: Creating %d mapper processes...\n", M);
    pid_t pids[M]; // Array to store child process IDs

    for (int i = 0; i < M; ++i) {
        pids[i] = fork(); // Create a child process [cite: 552]

        if (pids[i] < 0) { // Error occurred during fork
            perror("Mapper fork failed");
            // Kill already created children before exiting
            for (int k = 0; k < i; ++k) {
                 kill(pids[k], SIGTERM); // Send termination signal
            }
             exit(EXIT_FAILURE);
        } else if (pids[i] == 0) { // Child process code
             printf(" -> Mapper process %d (PID %d) started.\n", i + 1, getpid());
             run_mapper(i + 1, R, mind, maxd); // Run mapper logic [cite: 4696]
             exit(EXIT_SUCCESS); // Mapper finishes and exits [cite: 4709]
        }
        // Parent process continues the loop to create the next child
    }

    // Parent process waits for all mapper children to complete
    int status;
    int completed_mappers = 0;
    pid_t terminated_pid;
    printf("Step 2: Waiting for mapper processes to finish...\n");
    while (completed_mappers < M) {
        terminated_pid = wait(&status); // Wait for any child to terminate [cite: 743, 4710]
        if (terminated_pid < 0) {
            perror("Wait error");
            // Handle error, maybe attempt to kill remaining children
             exit(EXIT_FAILURE);
        }

        if (WIFEXITED(status)) {
            // Find which mapper finished
             for(int i=0; i<M; ++i) {
                 if(pids[i] == terminated_pid) {
                     printf(" -> Mapper process %d (PID %d) finished with status %d.\n", i + 1, terminated_pid, WEXITSTATUS(status));
                     pids[i] = -1; // Mark as finished
                     completed_mappers++;
                     break;
                 }
             }
        } else if (WIFSIGNALED(status)) {
            // Handle cases where child was terminated by a signal
             fprintf(stderr, "Mapper process %d terminated by signal %d.\n", terminated_pid, WTERMSIG(status));
             // Decide how to handle this, maybe exit or try to continue
             completed_mappers++; // Count it as completed to avoid infinite loop
        }
    }
    printf("Step 2: All mapper processes finished.\n");
    printf("------------------------------------\n");

// --- Step 3: Create Reducer Processes ---
    printf("Step 3: Creating shared memory and %d reducer processes...\n", R);

    // --- Create Shared Memory Segment ---
    const char *shm_name = "/cs342_proj1_shm"; // Unique name for shared memory object
    // Calculate size: 2^SHMSIZE bytes
    size_t shm_segment_size = (size_t)pow(2, shmsize_exponent);
    printf("  Shared memory size: %zu bytes (2^%d)\n", shm_segment_size, shmsize_exponent);

    // shm_open: Create or open a POSIX shared memory object
    // O_CREAT: Create if it doesn't exist
    // O_RDWR: Open for read-write access
    // O_TRUNC: Truncate size to 0 if it exists (ensures fresh start)
    // 0666: Permissions (readable/writable by owner, group, others)
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed in parent");
        // Cleanup split files before exiting
        // ...
        exit(EXIT_FAILURE);
    }

    // ftruncate: Set the size of the shared memory object
    if (ftruncate(shm_fd, shm_segment_size) == -1) {
        perror("ftruncate failed in parent");
        close(shm_fd);
        shm_unlink(shm_name); // Clean up shared memory object
        // Cleanup split files
        // ...
        exit(EXIT_FAILURE);
    }
    // Close the descriptor in the parent for now, children will open it themselves
    close(shm_fd);
    printf("  Shared memory segment '%s' created and sized.\n", shm_name);


    // --- Fork Reducer Processes ---
    pid_t reducer_pids[R]; // Array to store reducer process IDs
    for (int i = 0; i < R; ++i) {
        reducer_pids[i] = fork();

        if (reducer_pids[i] < 0) { // Error
            perror("Reducer fork failed");
            // Kill already created children and cleanup shm
            for (int k = 0; k < i; ++k) kill(reducer_pids[k], SIGTERM);
            shm_unlink(shm_name);
            exit(EXIT_FAILURE);
        } else if (reducer_pids[i] == 0) { // Child process (Reducer)
            printf(" -> Reducer process %d (PID %d) started.\n", i + 1, getpid());
            run_reducer(i + 1, M, R, shm_name, shm_segment_size);
            return EXIT_SUCCESS; // Reducer finishes and exits
        }
        // Parent continues loop
    }

    // --- Wait for Reducer Processes ---
    int reducer_status;
    int completed_reducers = 0;
    pid_t terminated_reducer_pid;
    printf("Step 3: Waiting for reducer processes to finish...\n");
    while (completed_reducers < R) {
        terminated_reducer_pid = wait(&reducer_status); // Wait for any child
        if (terminated_reducer_pid < 0) {
            perror("Reducer wait error");
            shm_unlink(shm_name); // Cleanup before exiting
            exit(EXIT_FAILURE);
        }

        if (WIFEXITED(reducer_status)) {
             for(int i=0; i<R; ++i) {
                 if(reducer_pids[i] == terminated_reducer_pid) {
                     printf(" -> Reducer process %d (PID %d) finished with status %d.\n", i + 1, terminated_reducer_pid, WEXITSTATUS(reducer_status));
                     reducer_pids[i] = -1; // Mark as finished
                     completed_reducers++;
                     break;
                 }
             }
        } else if (WIFSIGNALED(reducer_status)) {
            fprintf(stderr, "Reducer process %d terminated by signal %d.\n", terminated_reducer_pid, WTERMSIG(reducer_status));
            completed_reducers++;
        }
    }
    printf("Step 3: All reducer processes finished.\n");
    printf("------------------------------------\n");

    // --- Step 4: Finalization ---
    printf("Step 4: Finalizing output files...\n");

    // --- Merge output-k files into OUT1 ---
    printf("  Merging reducer outputs into '%s'...\n", out1_name);
    FILE *out1_file = fopen(out1_name, "w");
    if (!out1_file) {
        perror("Error opening final output file OUT1");
        shm_unlink(shm_name);
        exit(EXIT_FAILURE);
    }

    FILE *reducer_outputs[R];
    long current_dests[R];
    char *current_lines[R];
    size_t line_caps[R];
    int active_reducers = R;
    int min_index;
    long min_dest;

    // Initialize by opening all reducer outputs and reading the first line
    for (int i = 0; i < R; ++i) {
        char reducer_out_name[20];
        sprintf(reducer_out_name, "output-%d", i + 1);
        reducer_outputs[i] = fopen(reducer_out_name, "r");
        current_lines[i] = NULL;
        line_caps[i] = 0;
        if (!reducer_outputs[i]) {
            fprintf(stderr, "Warning: Could not open %s for merging.\n", reducer_out_name);
            current_dests[i] = LONG_MAX; // Treat as ended
            active_reducers--;
        } else {
            // Read the first line
            if (getline(&current_lines[i], &line_caps[i], reducer_outputs[i]) > 0) {
                current_dests[i] = atol(current_lines[i]); // Get dest number before ':'
            } else {
                // File is empty or read error
                fclose(reducer_outputs[i]);
                reducer_outputs[i] = NULL;
                 if(current_lines[i]) free(current_lines[i]);
                 current_lines[i] = NULL;
                current_dests[i] = LONG_MAX; // Mark as ended
                active_reducers--;
            }
        }
    }

    // K-way merge
    while (active_reducers > 0) {
        min_dest = LONG_MAX;
        min_index = -1;

        // Find the reducer file with the smallest current destination
        for (int i = 0; i < R; ++i) {
            if (reducer_outputs[i] != NULL && current_dests[i] < min_dest) {
                min_dest = current_dests[i];
                min_index = i;
            }
        }

        if (min_index == -1) break; // Should not happen if active_reducers > 0

        // Write the line from the min_index file to OUT1
        fprintf(out1_file, "%s", current_lines[min_index]);

        // Read the next line from that reducer file
        if (getline(&current_lines[min_index], &line_caps[min_index], reducer_outputs[min_index]) > 0) {
            current_dests[min_index] = atol(current_lines[min_index]);
        } else {
            // End of this file
            fclose(reducer_outputs[min_index]);
            reducer_outputs[min_index] = NULL;
            if(current_lines[min_index]) free(current_lines[min_index]);
            current_lines[min_index] = NULL;
            current_dests[min_index] = LONG_MAX; // Mark as ended
            active_reducers--;
        }
    }

    // Clean up merge resources
    fclose(out1_file);
    for (int i = 0; i < R; ++i) {
        if (reducer_outputs[i]) fclose(reducer_outputs[i]);
        if (current_lines[i]) free(current_lines[i]);
    }
     printf("  Merging into '%s' complete.\n", out1_name);


    // --- Process shared memory into OUT2 ---
    printf("  Processing shared memory into '%s'...\n", out2_name);
    shm_fd = shm_open(shm_name, O_RDONLY, 0666); // Open for reading
    if (shm_fd == -1) {
        perror("shm_open failed for reading in parent");
        shm_unlink(shm_name);
        exit(EXIT_FAILURE);
    }

    // mmap: Map the shared memory object into the parent's address space
    void *shm_ptr = mmap(NULL, shm_segment_size, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap failed in parent for reading");
        close(shm_fd);
        shm_unlink(shm_name);
        exit(EXIT_FAILURE);
    }
    close(shm_fd); // Descriptor no longer needed after mapping

    // Assuming shared memory contains DestCountPair structs sequentially
    // Calculate max possible pairs
    size_t max_pairs = shm_segment_size / sizeof(DestCountPair);
    DestCountPair *all_pairs = malloc(max_pairs * sizeof(DestCountPair));
     if (!all_pairs) {
         fprintf(stderr, "Failed to allocate memory for collecting shared memory data.\n");
         munmap(shm_ptr, shm_segment_size);
         shm_unlink(shm_name);
         exit(EXIT_FAILURE);
     }

    // Copy data from shared memory (need to know how many pairs are valid)
    // Simple approach: Iterate through shared memory until we find an invalid entry
    // (e.g., dest = 0 or -1, assuming valid dests are > 0) OR reach the end.
    // Reducers MUST ensure they write a terminator or only valid data.
    // Let's assume reducers write valid pairs contiguously and the rest is zeroed.
    DestCountPair *shm_pairs = (DestCountPair *)shm_ptr;
    size_t valid_pairs_count = 0;
    for (size_t i = 0; i < max_pairs; ++i) {
        if (shm_pairs[i].dest > 0) { // Assuming valid destinations are positive
            all_pairs[valid_pairs_count++] = shm_pairs[i];
        } else {
             // Optimization: If memory is zeroed, we might break early,
             // but safer to check the whole segment if reducers write sparsely.
             // For simplicity, let's assume contiguous writing per reducer block,
             // but reducer blocks might not fill the whole segment.
             // We'll just copy all non-zero entries.
        }
    }
     printf("  Found %zu valid (dest, count) pairs in shared memory.\n", valid_pairs_count);


    // Sort the collected pairs by destination
    qsort(all_pairs, valid_pairs_count, sizeof(DestCountPair), compare_dest_count);

    // Write sorted pairs to OUT2
    FILE *out2_file = fopen(out2_name, "w");
    if (!out2_file) {
        perror("Error opening final output file OUT2");
        munmap(shm_ptr, shm_segment_size);
        shm_unlink(shm_name);
        free(all_pairs);
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < valid_pairs_count; ++i) {
        fprintf(out2_file, "%ld %d\n", all_pairs[i].dest, all_pairs[i].count);
    }
    fclose(out2_file);
    free(all_pairs);
     printf("  Writing to '%s' complete.\n", out2_name);


    // --- Cleanup Shared Memory ---
    if (munmap(shm_ptr, shm_segment_size) == -1) {
        perror("munmap failed in parent");
        // Log error, but continue to unlink
    }
    if (shm_unlink(shm_name) == -1) {
        perror("shm_unlink failed in parent");
        // Log error
    } else {
         printf("  Shared memory segment '%s' unlinked.\n", shm_name);
    }

    // Optional: Cleanup intermediate, split, output-k files
    // As per instructions, we don't delete them.
    // ...

    printf("------------------------------------\n");
    printf("findsp finished successfully.\n");

    return EXIT_SUCCESS;
}


// --- Reducer Logic Function ---
void run_reducer(int reducer_id, int M, int R, const char *shm_name, size_t shm_segment_size) {
    // --- Data structures for collecting pairs ---
    DestSrcPair *pairs = NULL;
    size_t pair_count = 0;
    size_t pair_capacity = 1024; // Initial capacity
    pairs = malloc(pair_capacity * sizeof(DestSrcPair));
    if (!pairs) {
        fprintf(stderr, "[Reducer %d] Failed to allocate initial memory for pairs.\n", reducer_id);
        exit(EXIT_FAILURE);
    }

    // --- Read Intermediate Files ---
    char intermediate_filename[30];
    FILE *intermediate_file;
    long d, s;
    for (int i = 1; i <= M; ++i) {
        sprintf(intermediate_filename, "intermediate-%d-%d", i, reducer_id);
        intermediate_file = fopen(intermediate_filename, "r");
        if (!intermediate_file) {
            // It's possible a mapper didn't create a file for this reducer, which is okay.
            // fprintf(stderr, "[Reducer %d] Warning: Cannot open intermediate file %s: %s\n", reducer_id, intermediate_filename, strerror(errno));
            continue;
        }

        while (fscanf(intermediate_file, "%ld %ld", &d, &s) == 2) {
            // Add pair to dynamic array, resizing if necessary
            if (pair_count >= pair_capacity) {
                pair_capacity *= 2;
                DestSrcPair *temp = realloc(pairs, pair_capacity * sizeof(DestSrcPair));
                if (!temp) {
                    fprintf(stderr, "[Reducer %d] Failed to reallocate memory for pairs.\n", reducer_id);
                    fclose(intermediate_file);
                    free(pairs);
                    exit(EXIT_FAILURE);
                }
                pairs = temp;
            }
            pairs[pair_count].dest = d;
            pairs[pair_count].src = s;
            pair_count++;
        }
        fclose(intermediate_file);
    }
     // Optional: printf("[Reducer %d] Read %zu pairs from intermediate files.\n", reducer_id, pair_count);


    // --- Sort the pairs ---
    qsort(pairs, pair_count, sizeof(DestSrcPair), compare_dest_src);
    // Optional: printf("[Reducer %d] Pairs sorted.\n", reducer_id);


    // --- Process Sorted Data and Write Output ---
    char reducer_out_name[20];
    sprintf(reducer_out_name, "output-%d", reducer_id);
    FILE *output_file = fopen(reducer_out_name, "w");
     if (!output_file) {
        fprintf(stderr, "[Reducer %d] Error opening output file %s: %s\n", reducer_id, reducer_out_name, strerror(errno));
        free(pairs);
        exit(EXIT_FAILURE);
    }

    // --- Map Shared Memory ---
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        fprintf(stderr, "[Reducer %d] shm_open failed: %s\n", reducer_id, strerror(errno));
         fclose(output_file);
         free(pairs);
        exit(EXIT_FAILURE);
    }
    void *shm_ptr = mmap(NULL, shm_segment_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        fprintf(stderr, "[Reducer %d] mmap failed: %s\n", reducer_id, strerror(errno));
        close(shm_fd);
         fclose(output_file);
         free(pairs);
        exit(EXIT_FAILURE);
    }
    close(shm_fd);

    // Calculate this reducer's portion in shared memory
    size_t pairs_per_reducer_max = (shm_segment_size / sizeof(DestCountPair)) / R;
    size_t shm_offset_pairs = (reducer_id - 1) * pairs_per_reducer_max;
    DestCountPair *shm_dest_count_ptr = (DestCountPair *)shm_ptr + shm_offset_pairs;
    size_t shm_pairs_written = 0;


    // --- Iterate, Process, Write Output and Shared Memory ---
    if (pair_count > 0) {
        long current_dest = pairs[0].dest;
        long *current_sources = malloc(pair_count * sizeof(long)); // Over-estimate size
        if(!current_sources) {
            fprintf(stderr, "[Reducer %d] Failed to allocate memory for current_sources.\n", reducer_id);
            fclose(output_file);
            free(pairs);
            exit(EXIT_FAILURE);
        }
        int source_idx = 0;
        int unique_source_count = 0;

        for (size_t i = 0; i < pair_count; ++i) {
            if (pairs[i].dest != current_dest) {
                // --- Process previous destination ---
                // 1. Sort unique sources
                qsort(current_sources, unique_source_count, sizeof(long), compare_long);

                // 2. Write to output-k file
                fprintf(output_file, "%ld:", current_dest);
                for(int k=0; k < unique_source_count; ++k) {
                    fprintf(output_file, "%ld ", current_sources[k]);
                }
                fprintf(output_file, "\n");

                // 3. Write (dest, count) to shared memory (if space allows)
                if (shm_pairs_written < pairs_per_reducer_max) {
                    shm_dest_count_ptr[shm_pairs_written].dest = current_dest;
                    shm_dest_count_ptr[shm_pairs_written].count = unique_source_count;
                    shm_pairs_written++;
                } else {
                     fprintf(stderr, "[Reducer %d] Warning: Shared memory space exceeded for this reducer.\n", reducer_id);
                     // Stop writing to shared memory for this reducer
                }

                // --- Start processing new destination ---
                current_dest = pairs[i].dest;
                source_idx = 0;
                unique_source_count = 0;
                // Add first source for new destination
                current_sources[source_idx++] = pairs[i].src;
                unique_source_count++;

            } else {
                // Same destination, add source if it's unique
                if (source_idx == 0 || pairs[i].src != current_sources[source_idx-1]) {
                    current_sources[source_idx++] = pairs[i].src;
                    unique_source_count++;
                }
            }
        }

         // --- Process the very last destination group ---
         qsort(current_sources, unique_source_count, sizeof(long), compare_long);
         fprintf(output_file, "%ld:", current_dest);
         for(int k=0; k < unique_source_count; ++k) {
             fprintf(output_file, "%ld ", current_sources[k]);
         }
         fprintf(output_file, "\n");
         if (shm_pairs_written < pairs_per_reducer_max) {
             shm_dest_count_ptr[shm_pairs_written].dest = current_dest;
             shm_dest_count_ptr[shm_pairs_written].count = unique_source_count;
             shm_pairs_written++;
         } else {
              fprintf(stderr, "[Reducer %d] Warning: Shared memory space exceeded for this reducer.\n", reducer_id);
         }
         free(current_sources);
    }
    // Optional: Write a terminator (-1,-1) or zero out remaining part of shm block?
    // Let's rely on the parent checking for dest > 0 for simplicity now.


    // --- Cleanup ---
    fclose(output_file);
    free(pairs);
    if (munmap(shm_ptr, shm_segment_size) == -1) {
        fprintf(stderr, "[Reducer %d] munmap failed: %s\n", reducer_id, strerror(errno));
    }

    printf("------------------------------------\n");
    printf("findsp finished successfully.\n");

    return; // Use return for void function
}

// --- Mapper Logic Function ---
void run_mapper(int mapper_id, int R, long mind, long maxd) {
    // Open the corresponding split file
    char split_filename[20];
    sprintf(split_filename, "split-%d", mapper_id);
    FILE *split_file = fopen(split_filename, "r");
    if (!split_file) {
        fprintf(stderr, "[Mapper %d] Error opening split file %s: %s\n", mapper_id, split_filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Create intermediate files for each reducer
    FILE *intermediate_files[R];
    char intermediate_filename[30];
    for (int i = 0; i < R; ++i) {
        sprintf(intermediate_filename, "intermediate-%d-%d", mapper_id, i + 1);
        intermediate_files[i] = fopen(intermediate_filename, "w");
        if (!intermediate_files[i]) {
            fprintf(stderr, "[Mapper %d] Error creating intermediate file %s: %s\n", mapper_id, intermediate_filename, strerror(errno));
            // Close already opened files
            for (int k = 0; k < i; ++k) {
                fclose(intermediate_files[k]);
            }
            fclose(split_file);
            exit(EXIT_FAILURE);
        }
    }

    // Read split file line by line and process
    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;
    long src, dest;

    while ((read_len = getline(&line, &len, split_file)) != -1) {
        // Parse the line to extract source and destination
        if (sscanf(line, "%ld %ld", &src, &dest) == 2) {
            // Apply MIND and MAXD filters
            if (mind != -1 && dest < mind) continue;
            if (maxd != -1 && dest > maxd) continue;
            
            // Determine which reducer this pair should go to (hash by destination)
            int reducer_id = (dest % R) + 1;
            
            // Write to the appropriate intermediate file
            fprintf(intermediate_files[reducer_id - 1], "%ld %ld\n", dest, src);
        }
    }

    // Clean up
    fclose(split_file);
    for (int i = 0; i < R; ++i) {
        fclose(intermediate_files[i]);
    }
    if (line) {
        free(line);
    }
}

// --- Comparison Functions ---
int compare_dest_src(const void *a, const void *b) {
    DestSrcPair *pair_a = (DestSrcPair *)a;
    DestSrcPair *pair_b = (DestSrcPair *)b;
    
    // First compare by destination
    if (pair_a->dest != pair_b->dest) {
        return (pair_a->dest > pair_b->dest) ? 1 : -1;
    }
    // If destinations are equal, compare by source
    return (pair_a->src > pair_b->src) ? 1 : -1;
}

int compare_dest_count(const void *a, const void *b) {
    DestCountPair *pair_a = (DestCountPair *)a;
    DestCountPair *pair_b = (DestCountPair *)b;
    
    // Compare by destination
    if (pair_a->dest != pair_b->dest) {
        return (pair_a->dest > pair_b->dest) ? 1 : -1;
    }
    // If destinations are equal, compare by count
    return (pair_a->count > pair_b->count) ? 1 : -1;
}

int compare_long(const void *a, const void *b) {
    long *long_a = (long *)a;
    long *long_b = (long *)b;
    
    if (*long_a > *long_b) return 1;
    if (*long_a < *long_b) return -1;
    return 0;
}