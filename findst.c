#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define MAX_LINE_LENGTH 256
#define MAX_VERTICES 1000000

typedef struct {
    int destination;
    int count;
} DestCount;

typedef struct {
    int dest;
    int source;
} Pair;

typedef struct {
    int thread_id;
    int R;
    int MIND;
    int MAXD;
} MapperArgs;

typedef struct {
    int thread_id;
    int M;
    DestCount **shared_counts;
    int *count_sizes;
    pthread_mutex_t *mutex;
} ReducerArgs;

DestCount **global_shared_counts;
int *global_count_sizes;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

int compare_pairs(const void *a, const void *b) {
    Pair *pa = (Pair *)a;
    Pair *pb = (Pair *)b;
    
    if (pa->dest != pb->dest) {
        return pa->dest - pb->dest;
    }
    return pa->source - pb->source;
}

int compare_dest_counts(const void *a, const void *b) {
    DestCount *da = (DestCount *)a;
    DestCount *db = (DestCount *)b;
    return da->destination - db->destination;
}

void split_input_file(const char *input_file, int M) {
    FILE *infile = fopen(input_file, "r");
    if (!infile) {
        perror("Error opening input file");
        exit(1);
    }
    
    FILE **split_files = malloc(M * sizeof(FILE *));
    for (int i = 0; i < M; i++) {
        char split_name[64];
        sprintf(split_name, "split-%d", i + 1);
        split_files[i] = fopen(split_name, "w");
        if (!split_files[i]) {
            perror("Error creating split file");
            exit(1);
        }
    }
    
    char line[MAX_LINE_LENGTH];
    int file_index = 0;
    
    while (fgets(line, MAX_LINE_LENGTH, infile)) {
        fprintf(split_files[file_index], "%s", line);
        file_index = (file_index + 1) % M;
    }
    
    fclose(infile);
    for (int i = 0; i < M; i++) {
        fclose(split_files[i]);
    }
    free(split_files);
}

void *mapper_thread(void *arg) {
    MapperArgs *args = (MapperArgs *)arg;
    int mapper_id = args->thread_id;
    int R = args->R;
    int MIND = args->MIND;
    int MAXD = args->MAXD;
    
    char split_name[64];
    sprintf(split_name, "split-%d", mapper_id);
    
    FILE *split_file = fopen(split_name, "r");
    if (!split_file) {
        perror("Error opening split file in mapper");
        pthread_exit(NULL);
    }
    
    FILE **intermediate_files = malloc(R * sizeof(FILE *));
    for (int j = 0; j < R; j++) {
        char intermediate_name[64];
        sprintf(intermediate_name, "intermediate-%d-%d", mapper_id, j + 1);
        intermediate_files[j] = fopen(intermediate_name, "w");
        if (!intermediate_files[j]) {
            perror("Error creating intermediate file");
            pthread_exit(NULL);
        }
    }
    
    int source, dest;
    while (fscanf(split_file, "%d %d", &source, &dest) == 2) {
        if (MIND != -1 && dest < MIND) continue;
        if (MAXD != -1 && dest > MAXD) continue;
        
        int reducer_index = (dest % R);
        fprintf(intermediate_files[reducer_index], "%d %d\n", dest, source);
    }
    
    fclose(split_file);
    for (int j = 0; j < R; j++) {
        fclose(intermediate_files[j]);
    }
    free(intermediate_files);
    
    pthread_exit(NULL);
}

void *reducer_thread(void *arg) {
    ReducerArgs *args = (ReducerArgs *)arg;
    int reducer_id = args->thread_id;
    int M = args->M;
    
    Pair *all_pairs = malloc(MAX_VERTICES * sizeof(Pair));
    int pair_count = 0;
    
    for (int i = 1; i <= M; i++) {
        char intermediate_name[64];
        sprintf(intermediate_name, "intermediate-%d-%d", i, reducer_id);
        
        FILE *intermediate_file = fopen(intermediate_name, "r");
        if (!intermediate_file) continue;
        
        int dest, source;
        while (fscanf(intermediate_file, "%d %d", &dest, &source) == 2) {
            all_pairs[pair_count].dest = dest;
            all_pairs[pair_count].source = source;
            pair_count++;
        }
        fclose(intermediate_file);
    }
    
    qsort(all_pairs, pair_count, sizeof(Pair), compare_pairs);
    
    char output_name[64];
    sprintf(output_name, "output-%d", reducer_id);
    FILE *output_file = fopen(output_name, "w");
    if (!output_file) {
        perror("Error creating output file");
        pthread_exit(NULL);
    }
    
    DestCount *local_counts = malloc(MAX_VERTICES * sizeof(DestCount));
    int local_count_size = 0;
    
    if (pair_count > 0) {
        int current_dest = all_pairs[0].dest;
        int *sources = malloc(MAX_VERTICES * sizeof(int));
        int source_count = 0;
        int prev_source = -1;
        
        for (int i = 0; i < pair_count; i++) {
            if (all_pairs[i].dest != current_dest) {
                fprintf(output_file, "%d:", current_dest);
                for (int j = 0; j < source_count; j++) {
                    fprintf(output_file, " %d", sources[j]);
                }
                fprintf(output_file, "\n");
                
                local_counts[local_count_size].destination = current_dest;
                local_counts[local_count_size].count = source_count;
                local_count_size++;
                
                current_dest = all_pairs[i].dest;
                source_count = 0;
                prev_source = -1;
            }
            
            if (all_pairs[i].source != prev_source) {
                sources[source_count++] = all_pairs[i].source;
                prev_source = all_pairs[i].source;
            }
        }
        
        fprintf(output_file, "%d:", current_dest);
        for (int j = 0; j < source_count; j++) {
            fprintf(output_file, " %d", sources[j]);
        }
        fprintf(output_file, "\n");
        
        local_counts[local_count_size].destination = current_dest;
        local_counts[local_count_size].count = source_count;
        local_count_size++;
        
        free(sources);
    }
    
    pthread_mutex_lock(args->mutex);
    args->shared_counts[reducer_id - 1] = local_counts;
    args->count_sizes[reducer_id - 1] = local_count_size;
    pthread_mutex_unlock(args->mutex);
    
    fclose(output_file);
    free(all_pairs);
    
    pthread_exit(NULL);
}

void merge_outputs(int R, const char *out1, const char *out2, DestCount **shared_counts, int *count_sizes) {
    typedef struct {
        int dest;
        char sources[1024];
    } OutputLine;
    
    OutputLine *all_lines = malloc(MAX_VERTICES * sizeof(OutputLine));
    int line_count = 0;
    
    for (int i = 1; i <= R; i++) {
        char output_name[64];
        sprintf(output_name, "output-%d", i);
        
        FILE *output_file = fopen(output_name, "r");
        if (!output_file) continue;
        
        char line[2048];
        while (fgets(line, 2048, output_file)) {
            int dest;
            char sources[1024];
            
            if (sscanf(line, "%d:%[^\n]", &dest, sources) == 2) {
                all_lines[line_count].dest = dest;
                strcpy(all_lines[line_count].sources, sources);
                line_count++;
            }
        }
        fclose(output_file);
    }
    
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - i - 1; j++) {
            if (all_lines[j].dest > all_lines[j + 1].dest) {
                OutputLine temp = all_lines[j];
                all_lines[j] = all_lines[j + 1];
                all_lines[j + 1] = temp;
            }
        }
    }
    
    FILE *out1_file = fopen(out1, "w");
    if (!out1_file) {
        perror("Error creating OUT1 file");
        exit(1);
    }
    
    for (int i = 0; i < line_count; i++) {
        fprintf(out1_file, "%d:%s\n", all_lines[i].dest, all_lines[i].sources);
    }
    fclose(out1_file);
    
    DestCount *all_counts = malloc(MAX_VERTICES * sizeof(DestCount));
    int count_total = 0;
    
    for (int i = 0; i < R; i++) {
        if (shared_counts[i] != NULL) {
            for (int j = 0; j < count_sizes[i]; j++) {
                all_counts[count_total++] = shared_counts[i][j];
            }
        }
    }
    
    qsort(all_counts, count_total, sizeof(DestCount), compare_dest_counts);
    
    FILE *out2_file = fopen(out2, "w");
    if (!out2_file) {
        perror("Error creating OUT2 file");
        exit(1);
    }
    
    for (int i = 0; i < count_total; i++) {
        fprintf(out2_file, "%d: %d\n", all_counts[i].destination, all_counts[i].count);
    }
    fclose(out2_file);
    
    free(all_lines);
    free(all_counts);
}

int main(int argc, char *argv[]) {
    if (argc != 9) {
        fprintf(stderr, "Usage: %s INFILE M R OUT1 OUT2 MIND MAXD SHMSIZE\n", argv[0]);
        exit(1);
    }
    
    const char *input_file = argv[1];
    int M = atoi(argv[2]);
    int R = atoi(argv[3]);
    const char *out1 = argv[4];
    const char *out2 = argv[5];
    int MIND = atoi(argv[6]);
    int MAXD = atoi(argv[7]);
    // SHMSIZE is not used in thread version, but kept for API compatibility
    
    if (M < 1 || M > 20) {
        fprintf(stderr, "M must be between 1 and 20\n");
        exit(1);
    }
    
    if (R < 1 || R > 10) {
        fprintf(stderr, "R must be between 1 and 10\n");
        exit(1);
    }
    
    split_input_file(input_file, M);
    
    pthread_t *mapper_threads = malloc(M * sizeof(pthread_t));
    MapperArgs *mapper_args = malloc(M * sizeof(MapperArgs));
    
    for (int i = 0; i < M; i++) {
        mapper_args[i].thread_id = i + 1;
        mapper_args[i].R = R;
        mapper_args[i].MIND = MIND;
        mapper_args[i].MAXD = MAXD;
        
        if (pthread_create(&mapper_threads[i], NULL, mapper_thread, &mapper_args[i]) != 0) {
            perror("Failed to create mapper thread");
            exit(1);
        }
    }
    
    for (int i = 0; i < M; i++) {
        pthread_join(mapper_threads[i], NULL);
    }
    
    global_shared_counts = calloc(R, sizeof(DestCount *));
    global_count_sizes = calloc(R, sizeof(int));
    
    pthread_t *reducer_threads = malloc(R * sizeof(pthread_t));
    ReducerArgs *reducer_args = malloc(R * sizeof(ReducerArgs));
    
    for (int i = 0; i < R; i++) {
        reducer_args[i].thread_id = i + 1;
        reducer_args[i].M = M;
        reducer_args[i].shared_counts = global_shared_counts;
        reducer_args[i].count_sizes = global_count_sizes;
        reducer_args[i].mutex = &count_mutex;
        
        if (pthread_create(&reducer_threads[i], NULL, reducer_thread, &reducer_args[i]) != 0) {
            perror("Failed to create reducer thread");
            exit(1);
        }
    }
    
    for (int i = 0; i < R; i++) {
        pthread_join(reducer_threads[i], NULL);
    }
    
    merge_outputs(R, out1, out2, global_shared_counts, global_count_sizes);
    
    for (int i = 0; i < R; i++) {
        if (global_shared_counts[i] != NULL) {
            free(global_shared_counts[i]);
        }
    }
    free(global_shared_counts);
    free(global_count_sizes);
    free(mapper_threads);
    free(mapper_args);
    free(reducer_threads);
    free(reducer_args);
    
    return 0;
}