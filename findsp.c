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

void mapper_process(int mapper_id, int R, int MIND, int MAXD) {
    char split_name[64];
    sprintf(split_name, "split-%d", mapper_id);
    
    FILE *split_file = fopen(split_name, "r");
    if (!split_file) {
        perror("Error opening split file in mapper");
        exit(1);
    }
    
    FILE **intermediate_files = malloc(R * sizeof(FILE *));
    for (int j = 0; j < R; j++) {
        char intermediate_name[64];
        sprintf(intermediate_name, "intermediate-%d-%d", mapper_id, j + 1);
        intermediate_files[j] = fopen(intermediate_name, "w");
        if (!intermediate_files[j]) {
            perror("Error creating intermediate file");
            exit(1);
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
}

void reducer_process(int reducer_id, int M, int R, DestCount *shared_mem, int shared_mem_size) {
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
        exit(1);
    }
    
    int shared_index = (reducer_id - 1) * (shared_mem_size / sizeof(DestCount) / R);
    int count_index = 0;
    
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
                
                if (shared_index + count_index < shared_mem_size / sizeof(DestCount)) {
                    shared_mem[shared_index + count_index].destination = current_dest;
                    shared_mem[shared_index + count_index].count = source_count;
                    count_index++;
                }
                
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
        
        if (shared_index + count_index < shared_mem_size / sizeof(DestCount)) {
            shared_mem[shared_index + count_index].destination = current_dest;
            shared_mem[shared_index + count_index].count = source_count;
            count_index++;
        }
        
        free(sources);
    }
    
    for (int i = count_index; i < shared_mem_size / sizeof(DestCount) / R && 
         shared_index + i < shared_mem_size / sizeof(DestCount); i++) {
        shared_mem[shared_index + i].destination = -1;
    }
    
    fclose(output_file);
    free(all_pairs);
}

void merge_outputs(int R, const char *out1, const char *out2, DestCount *shared_mem, int shared_mem_size) {
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
    
    for (int i = 0; i < shared_mem_size / sizeof(DestCount); i++) {
        if (shared_mem[i].destination != -1 && shared_mem[i].destination != 0) {
            all_counts[count_total++] = shared_mem[i];
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
    int SHMSIZE = atoi(argv[8]);
    
    if (M < 1 || M > 20) {
        fprintf(stderr, "M must be between 1 and 20\n");
        exit(1);
    }
    
    if (R < 1 || R > 10) {
        fprintf(stderr, "R must be between 1 and 10\n");
        exit(1);
    }
    
    split_input_file(input_file, M);
    
    for (int i = 1; i <= M; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            mapper_process(i, R, MIND, MAXD);
            exit(0);
        } else if (pid < 0) {
            perror("Fork failed for mapper");
            exit(1);
        }
    }
    
    for (int i = 0; i < M; i++) {
        wait(NULL);
    }
    
    int shared_mem_size = 1 << SHMSIZE;
    
    int shm_fd = shm_open("/findsp_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    
    if (ftruncate(shm_fd, shared_mem_size) == -1) {
        perror("ftruncate");
        exit(1);
    }
    
    DestCount *shared_mem = mmap(NULL, shared_mem_size, PROT_READ | PROT_WRITE, 
                                 MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    
    memset(shared_mem, 0, shared_mem_size);
    
    for (int i = 1; i <= R; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            reducer_process(i, M, R, shared_mem, shared_mem_size);
            exit(0);
        } else if (pid < 0) {
            perror("Fork failed for reducer");
            exit(1);
        }
    }
    
    for (int i = 0; i < R; i++) {
        wait(NULL);
    }
    
    merge_outputs(R, out1, out2, shared_mem, shared_mem_size);
    
    if (munmap(shared_mem, shared_mem_size) == -1) {
        perror("munmap");
    }
    
    if (shm_unlink("/findsp_shm") == -1) {
        perror("shm_unlink");
    }
    
    close(shm_fd);
    
    return 0;
}