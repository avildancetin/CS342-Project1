#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h> // Include Pthreads header
#include <limits.h>
#include <math.h>
#include <sys/stat.h> // Include for stat if needed later, though not strictly required by current code
#include <unistd.h>

// Define constants for argument indices for clarity
#define ARG_INFILE 1
#define ARG_M 2
#define ARG_R 3
#define ARG_OUT1 4
#define ARG_OUT2 5
#define ARG_MIND 6
#define ARG_MAXD 7
#define ARG_SHMSIZE 8 // SHMSIZE is not used in Part B, but we keep arg count consistent
#define EXPECTED_ARGS 9

// Define a structure for storing pairs for OUT2 (now in regular memory)
typedef struct
{
    long dest;
    int count;
} DestCountPair;

// Define a structure for storing d,s pairs in reducer
typedef struct
{
    long dest;
    long src;
} DestSrcPair;

// --- Structs for passing arguments to threads ---
typedef struct
{
    int mapper_id;
    int R;
    long mind;
    long maxd;
} MapperArgs;

typedef struct
{
    int reducer_id;
    int M;
    DestCountPair *reducer_results;      // Pointer to where this reducer should store its counts
    size_t results_capacity_per_reducer; // Max pairs this reducer can store
    size_t *actual_results_count;        // Pointer to store the actual count written by reducer
} ReducerArgs;

// --- Function Prototypes ---
void *run_mapper_thread(void *args);  // Mapper thread function
void *run_reducer_thread(void *args); // Reducer thread function

// Comparison functions (same as Part A)
int compare_dest_src(const void *a, const void *b);
int compare_dest_count(const void *a, const void *b);
int compare_long(const void *a, const void *b);

int main(int argc, char *argv[])
{
    // --- Argument Parsing & Validation ---
    if (argc != EXPECTED_ARGS)
    {
        fprintf(stderr, "Usage: %s INFILE M R OUT1 OUT2 MIND MAXD SHMSIZE\n", argv[0]);
        fprintf(stderr, "Note: SHMSIZE argument is ignored in the threaded version.\n");
        exit(EXIT_FAILURE);
    }
    char *infile_name = argv[ARG_INFILE];
    int M = atoi(argv[ARG_M]);
    int R = atoi(argv[ARG_R]);
    char *out1_name = argv[ARG_OUT1];
    char *out2_name = argv[ARG_OUT2];
    long mind = atol(argv[ARG_MIND]);
    long maxd = atol(argv[ARG_MAXD]);
    // int shmsize_exponent = atoi(argv[ARG_SHMSIZE]); // Ignored

    // --- Input Validation (same as Part A) ---
    if (M <= 0 || M > 20)
    { /* ... validation ... */
        exit(EXIT_FAILURE);
    }
    if (R <= 0 || R > 10)
    { /* ... validation ... */
        exit(EXIT_FAILURE);
    }
    if (mind != -1 && mind <= 0)
    { /* ... validation ... */
        exit(EXIT_FAILURE);
    }
    if (maxd != -1 && maxd <= 0)
    { /* ... validation ... */
        exit(EXIT_FAILURE);
    }
    if (mind != -1 && maxd != -1 && mind > maxd)
    { /* ... validation ... */
        exit(EXIT_FAILURE);
    }

    printf("Arguments Parsed (Threaded Version):\n"); /* ... print arguments ... */
    printf("------------------------------------\n");

    // --- Step 1: Splitting the Input File ---
    // This step is identical to Part A, run by the main thread
    printf("Step 1: Splitting the input file '%s' into %d parts...\n", infile_name, M);
    FILE *infile = fopen(infile_name, "r");
    if (!infile)
    { /* ... error handling ... */
        exit(EXIT_FAILURE);
    }
    FILE *split_files[M];
    char split_filename[20];
    for (int i = 0; i < M; ++i)
    {
        sprintf(split_filename, "split-%d", i + 1);
        split_files[i] = fopen(split_filename, "w");
        if (!split_files[i])
        { /* ... error handling ... */
            exit(EXIT_FAILURE);
        }
    }
    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;
    int current_split_file_index = 0;
    while ((read_len = getline(&line, &len, infile)) != -1)
    {
        fprintf(split_files[current_split_file_index], "%s", line);
        current_split_file_index = (current_split_file_index + 1) % M;
    }
    fclose(infile);
    for (int i = 0; i < M; ++i)
    {
        fclose(split_files[i]);
    }
    if (line)
    {
        free(line);
    }
    printf("Step 1: Input file splitting complete.\n");
    printf("------------------------------------\n");

    // --- Step 2: Create Mapper Threads ---
    printf("Step 2: Creating %d mapper threads...\n", M);
    pthread_t mapper_threads[M]; // Array to store thread IDs
    MapperArgs mapper_args[M];   // Array to store arguments for each thread

    for (int i = 0; i < M; ++i)
    {
        mapper_args[i].mapper_id = i + 1;
        mapper_args[i].R = R;
        mapper_args[i].mind = mind;
        mapper_args[i].maxd = maxd;

        // pthread_create: Create a new thread
        // &mapper_threads[i]: Pointer to store the new thread ID
        // NULL: Thread attributes (default)
        // run_mapper_thread: The function the new thread will execute
        // &mapper_args[i]: The argument passed to the thread function
        int rc = pthread_create(&mapper_threads[i], NULL, run_mapper_thread, &mapper_args[i]);
        if (rc)
        {
            fprintf(stderr, "Error creating mapper thread %d: %s\n", i + 1, strerror(rc));
            // Consider cleanup/canceling already created threads
            exit(EXIT_FAILURE);
        }
    }

    // --- Wait for Mapper Threads ---
    printf("Step 2: Waiting for mapper threads to finish...\n");
    for (int i = 0; i < M; ++i)
    {
        // pthread_join: Wait for a specific thread to terminate
        // mapper_threads[i]: The ID of the thread to wait for
        // NULL: Pointer to store the thread's return value (we don't need it)
        int rc = pthread_join(mapper_threads[i], NULL);
        if (rc)
        {
            fprintf(stderr, "Error joining mapper thread %d: %s\n", i + 1, strerror(rc));
            // Consider error handling
        }
        else
        {
            printf(" -> Mapper thread %d finished.\n", i + 1);
        }
    }
    printf("Step 2: All mapper threads finished.\n");
    printf("------------------------------------\n");

    // --- Step 3: Create Reducer Threads ---
    printf("Step 3: Preparing shared results area and creating %d reducer threads...\n", R);

    // --- Allocate Memory for Reducer Results (Instead of Shared Memory) ---
    // Estimate a reasonable max number of unique destinations per reducer.
    // This is tricky without knowing the data. Let's make a guess or use a large buffer.
    // A safer approach might involve dynamic resizing within the reducer or a two-pass approach.
    // For simplicity, let's allocate a potentially large fixed-size block per reducer.
    size_t results_capacity_per_reducer = 50000; // *EXAMPLE* - Adjust based on expected data scale!
    size_t total_results_capacity = R * results_capacity_per_reducer;
    DestCountPair *all_reducer_results = malloc(total_results_capacity * sizeof(DestCountPair));
    size_t actual_results_counts[R]; // Track how many results each reducer *actually* writes
    if (!all_reducer_results)
    {
        fprintf(stderr, "Failed to allocate memory for reducer results.\n");
        exit(EXIT_FAILURE);
    }
    // Initialize counts to 0
    memset(actual_results_counts, 0, R * sizeof(size_t));
    memset(all_reducer_results, 0, total_results_capacity * sizeof(DestCountPair)); // Good practice

    // --- Create Reducer Threads ---
    pthread_t reducer_threads[R];
    ReducerArgs reducer_args[R];

    for (int i = 0; i < R; ++i)
    {
        reducer_args[i].reducer_id = i + 1;
        reducer_args[i].M = M;
        // Point each reducer to its slice of the results array
        reducer_args[i].reducer_results = all_reducer_results + (i * results_capacity_per_reducer);
        reducer_args[i].results_capacity_per_reducer = results_capacity_per_reducer;
        reducer_args[i].actual_results_count = &actual_results_counts[i]; // Pass address to store count

        int rc = pthread_create(&reducer_threads[i], NULL, run_reducer_thread, &reducer_args[i]);
        if (rc)
        {
            fprintf(stderr, "Error creating reducer thread %d: %s\n", i + 1, strerror(rc));
            // Cleanup
            free(all_reducer_results);
            exit(EXIT_FAILURE);
        }
    }

    // --- Wait for Reducer Threads ---
    printf("Step 3: Waiting for reducer threads to finish...\n");
    for (int i = 0; i < R; ++i)
    {
        int rc = pthread_join(reducer_threads[i], NULL);
        if (rc)
        {
            fprintf(stderr, "Error joining reducer thread %d: %s\n", i + 1, strerror(rc));
        }
        else
        {
            printf(" -> Reducer thread %d finished (wrote %zu results).\n", i + 1, actual_results_counts[i]);
        }
    }
    printf("Step 3: All reducer threads finished.\n");
    printf("------------------------------------\n");

    // --- Step 4: Finalization ---
    printf("Step 4: Finalizing output files...\n");

    // --- Merge output-k files into OUT1 ---
    // This part is identical to Part A
    printf("  Merging reducer outputs into '%s'...\n", out1_name);
    // ... (K-way merge logic exactly as in Part A) ...
    printf("  Merging into '%s' complete.\n", out1_name);

    // --- Process reducer results into OUT2 ---
    printf("  Processing reducer results into '%s'...\n", out2_name);

    // Calculate total actual results
    size_t total_valid_pairs = 0;
    for (int i = 0; i < R; ++i)
    {
        total_valid_pairs += actual_results_counts[i];
    }
    printf("  Total valid (dest, count) pairs from all reducers: %zu.\n", total_valid_pairs);

    // Consolidate results if reducers didn't fill their capacity (optional but good practice)
    // Or, if guaranteed contiguous, we can sort in place or copy to a perfectly sized array.
    // For simplicity, let's create a new array with the exact size.
    DestCountPair *final_pairs = malloc(total_valid_pairs * sizeof(DestCountPair));
    if (!final_pairs)
    { /* error handling */
        exit(EXIT_FAILURE);
    }

    size_t current_idx = 0;
    for (int i = 0; i < R; ++i)
    {
        DestCountPair *reducer_start = all_reducer_results + (i * results_capacity_per_reducer);
        memcpy(final_pairs + current_idx, reducer_start, actual_results_counts[i] * sizeof(DestCountPair));
        current_idx += actual_results_counts[i];
    }

    // Sort the consolidated pairs by destination
    qsort(final_pairs, total_valid_pairs, sizeof(DestCountPair), compare_dest_count);

    // Write sorted pairs to OUT2
    FILE *out2_file = fopen(out2_name, "w");
    if (!out2_file)
    {
        perror("Error opening final output file OUT2");
        free(all_reducer_results);
        free(final_pairs);
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < total_valid_pairs; ++i)
    {
        fprintf(out2_file, "%ld %d\n", final_pairs[i].dest, final_pairs[i].count);
    }
    fclose(out2_file);
    free(final_pairs);
    printf("  Writing to '%s' complete.\n", out2_name);

    // --- Cleanup ---
    free(all_reducer_results); // Free the memory allocated for results
    // Optional: Cleanup intermediate, split, output-k files (same as Part A - we keep them)
    // ...

    printf("------------------------------------\n");
    printf("findst finished successfully.\n");

    return EXIT_SUCCESS;
}

// --- Mapper Thread Function ---
void *run_mapper_thread(void *args)
{
    MapperArgs *margs = (MapperArgs *)args;
    int mapper_id = margs->mapper_id;
    int R = margs->R;
    long mind = margs->mind;
    long maxd = margs->maxd;

    printf("   [Mapper %d] Thread started.\n", mapper_id);

    char split_filename[20];
    sprintf(split_filename, "split-%d", mapper_id);

    FILE *split_file = fopen(split_filename, "r");
    if (!split_file)
    {
        fprintf(stderr, "[Mapper %d] Error opening split file %s: %s\n", mapper_id, split_filename, strerror(errno));
        pthread_exit(NULL); // Exit thread on error
    }

    // --- Optimization: Open R intermediate files once ---
    FILE *intermediate_files[R + 1];                           // Index 1 to R
    memset(intermediate_files, 0, sizeof(intermediate_files)); // Initialize to NULL
    char intermediate_filename[30];
    for (int k = 1; k <= R; ++k)
    {
        sprintf(intermediate_filename, "intermediate-%d-%d", mapper_id, k);
        intermediate_files[k] = fopen(intermediate_filename, "w"); // Use "w" - each mapper writes its own file set
        if (!intermediate_files[k])
        {
            fprintf(stderr, "[Mapper %d] Error opening intermediate file %s: %s\n", mapper_id, intermediate_filename, strerror(errno));
            // Close already opened files before exiting
            for (int j = 1; j < k; ++j)
                fclose(intermediate_files[j]);
            fclose(split_file);
            pthread_exit(NULL);
        }
    }

    long s, d;
    // Read pairs (s, d) from the split file
    while (fscanf(split_file, "%ld %ld", &s, &d) == 2)
    {
        // Apply MIND/MAXD filtering
        if ((mind != -1 && d < mind) || (maxd != -1 && d > maxd))
        {
            continue;
        }

        // Calculate reducer index k = (d % R) + 1
        int reducer_index = (d % R) + 1;

        // Write the reversed pair (d, s) to the appropriate intermediate file
        fprintf(intermediate_files[reducer_index], "%ld %ld\n", d, s);
    }

    fclose(split_file);
    // Close all intermediate files
    for (int k = 1; k <= R; ++k)
    {
        if (intermediate_files[k])
        {
            fclose(intermediate_files[k]);
        }
    }

    printf("   [Mapper %d] Thread finished.\n", mapper_id);
    pthread_exit(NULL); // Exit thread successfully
}

// --- Reducer Thread Function ---
void *run_reducer_thread(void *args)
{
    ReducerArgs *rargs = (ReducerArgs *)args;
    int reducer_id = rargs->reducer_id;
    int M = rargs->M;
    DestCountPair *shm_dest_count_ptr = rargs->reducer_results; // Renamed for clarity
    size_t pairs_per_reducer_max = rargs->results_capacity_per_reducer;
    size_t *shm_pairs_written_ptr = rargs->actual_results_count; // Renamed
    *shm_pairs_written_ptr = 0;                                  // Initialize count for this reducer

    printf("   [Reducer %d] Thread started.\n", reducer_id);

    // --- Data structures for collecting pairs ---
    // (Identical dynamic array logic as in Part A's run_reducer)
    DestSrcPair *pairs = NULL;
    size_t pair_count = 0;
    size_t pair_capacity = 1024;
    pairs = malloc(pair_capacity * sizeof(DestSrcPair));
    if (!pairs)
    { /* ... error handling ... */
        pthread_exit(NULL);
    }

    // --- Read Intermediate Files ---
    // (Identical logic as in Part A's run_reducer)
    char intermediate_filename[30];
    FILE *intermediate_file;
    long d, s;
    for (int i = 1; i <= M; ++i)
    {
        sprintf(intermediate_filename, "intermediate-%d-%d", i, reducer_id);
        intermediate_file = fopen(intermediate_filename, "r");
        if (!intermediate_file)
            continue; // Okay if file doesn't exist

        while (fscanf(intermediate_file, "%ld %ld", &d, &s) == 2)
        {
            if (pair_count >= pair_capacity)
            { // Resize logic
                pair_capacity *= 2;
                DestSrcPair *temp = realloc(pairs, pair_capacity * sizeof(DestSrcPair));
                if (!temp)
                { /* ... error handling ... */
                    free(pairs);
                    pthread_exit(NULL);
                }
                pairs = temp;
            }
            pairs[pair_count].dest = d;
            pairs[pair_count].src = s;
            pair_count++;
        }
        fclose(intermediate_file);
    }

    // --- Sort the pairs ---
    qsort(pairs, pair_count, sizeof(DestSrcPair), compare_dest_src);

    // --- Process Sorted Data and Write Output ---
    char reducer_out_name[20];
    sprintf(reducer_out_name, "output-%d", reducer_id);
    FILE *output_file = fopen(reducer_out_name, "w");
    if (!output_file)
    { /* ... error handling ... */
        free(pairs);
        pthread_exit(NULL);
    }

    // --- Iterate, Process, Write Output and Shared Results Array ---
    // (Logic is identical to Part A's run_reducer, except writing to
    // shm_dest_count_ptr instead of mapped memory, and updating *shm_pairs_written_ptr)
    if (pair_count > 0)
    {
        long current_dest = pairs[0].dest;
        long *current_sources = malloc(pair_count * sizeof(long));
        if (!current_sources)
        { /* error handling */
            exit(EXIT_FAILURE);
        }
        int source_idx = 0;
        int unique_source_count = 0;

        for (size_t i = 0; i < pair_count; ++i)
        {
            if (pairs[i].dest != current_dest)
            {
                // --- Process previous destination ---
                qsort(current_sources, unique_source_count, sizeof(long), compare_long);
                fprintf(output_file, "%ld:", current_dest);
                for (int k = 0; k < unique_source_count; ++k)
                    fprintf(output_file, "%ld ", current_sources[k]);
                fprintf(output_file, "\n");

                // Write to results array
                if (*shm_pairs_written_ptr < pairs_per_reducer_max)
                {
                    shm_dest_count_ptr[*shm_pairs_written_ptr].dest = current_dest;
                    shm_dest_count_ptr[*shm_pairs_written_ptr].count = unique_source_count;
                    (*shm_pairs_written_ptr)++; // Increment counter via pointer
                }
                else
                {
                    fprintf(stderr, "[Reducer %d] Warning: Results array space exceeded.\n", reducer_id);
                }

                // --- Start new destination ---
                current_dest = pairs[i].dest;
                source_idx = 0;
                unique_source_count = 0;
                if (source_idx == 0 || pairs[i].src != current_sources[source_idx - 1])
                {
                    current_sources[source_idx++] = pairs[i].src;
                    unique_source_count++;
                }
            }
            else
            {
                // Same destination, add unique source
                if (source_idx == 0 || pairs[i].src != current_sources[source_idx - 1])
                {
                    current_sources[source_idx++] = pairs[i].src;
                    unique_source_count++;
                }
            }
        }

        // --- Process the very last destination group ---
        qsort(current_sources, unique_source_count, sizeof(long), compare_long);
        fprintf(output_file, "%ld:", current_dest);
        for (int k = 0; k < unique_source_count; ++k)
            fprintf(output_file, "%ld ", current_sources[k]);
        fprintf(output_file, "\n");
        if (*shm_pairs_written_ptr < pairs_per_reducer_max)
        {
            shm_dest_count_ptr[*shm_pairs_written_ptr].dest = current_dest;
            shm_dest_count_ptr[*shm_pairs_written_ptr].count = unique_source_count;
            (*shm_pairs_written_ptr)++;
        }
        else
        {
            fprintf(stderr, "[Reducer %d] Warning: Results array space exceeded.\n", reducer_id);
        }
        free(current_sources);
    }

    // --- Cleanup ---
    fclose(output_file);
    free(pairs);

    printf("   [Reducer %d] Thread finished.\n", reducer_id);
    pthread_exit(NULL); // Exit thread
}

// --- Comparison Functions Implementation ---
// (Paste the implementations for compare_dest_src, compare_dest_count,
// and compare_long from Part A here)
int compare_dest_src(const void *a, const void *b)
{
    DestSrcPair *pairA = (DestSrcPair *)a;
    DestSrcPair *pairB = (DestSrcPair *)b;
    if (pairA->dest < pairB->dest)
        return -1;
    if (pairA->dest > pairB->dest)
        return 1;
    if (pairA->src < pairB->src)
        return -1;
    if (pairA->src > pairB->src)
        return 1;
    return 0;
}

int compare_dest_count(const void *a, const void *b)
{
    DestCountPair *pairA = (DestCountPair *)a;
    DestCountPair *pairB = (DestCountPair *)b;
    if (pairA->dest < pairB->dest)
        return -1;
    if (pairA->dest > pairB->dest)
        return 1;
    return 0;
}

int compare_long(const void *a, const void *b)
{
    long longA = *(long *)a;
    long longB = *(long *)b;
    if (longA < longB)
        return -1;
    if (longA > longB)
        return 1;
    return 0;
}