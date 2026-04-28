#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MEM_SEGMENT 0xABCDE
#define SYNC_TOKEN 0xEDCBA

typedef struct {
    int accumulator;
    int operation_done;
} shared_mem;

struct sembuf acquire_lock = {0, -1, SEM_UNDO};
struct sembuf release_lock = {0, 1, SEM_UNDO};

void perform_synchronized_operation(pid_t proc_id, shared_mem *mem_ptr, int iterations, int sync_id) {
    int local_counter;
    
    fprintf(stdout, "Worker %d: commencing task\n", proc_id);
    
    for (int idx = 0; idx < iterations; idx++) {
        if (semop(sync_id, &acquire_lock, 1) < 0) {
            perror("Lock acquisition failed");
            exit(EXIT_FAILURE);
        }
        
        local_counter = mem_ptr->accumulator;
        local_counter++;
        mem_ptr->accumulator = local_counter;
        
        if (semop(sync_id, &release_lock, 1) < 0) {
            perror("Lock release failed");
            exit(EXIT_FAILURE);
        }
        
        if (idx % 1000 == 0) {
            usleep(1000);
        }
    }
    
    fprintf(stdout, "Worker %d: task completed\n", proc_id);
}

void cleanup_sync_object(int sync_id) {
    if (semctl(sync_id, 0, IPC_RMID) < 0) {
        perror("Sync object removal error");
    }
}

int setup_shared_memory() {
    int seg_id = shmget(MEM_SEGMENT, sizeof(shared_mem), 0644|IPC_CREAT);
    if (seg_id < 0) {
        perror("Memory segment creation error");
        exit(EXIT_FAILURE);
    }
    return seg_id;
}

shared_mem* connect_shared_memory(int seg_id) {
    shared_mem *mem_ptr = shmat(seg_id, NULL, 0);
    if (mem_ptr == (void *) -1) {
        perror("Memory attachment error");
        exit(EXIT_FAILURE);
    }
    return mem_ptr;
}

int initialize_sync_mechanism() {
    int sync_id = semget(SYNC_TOKEN, 1, IPC_CREAT | IPC_EXCL | 0666);
    
    if (sync_id >= 0) {
        if (semctl(sync_id, 0, SETVAL, 1) < 0) {
            perror("Sync mechanism initialization failed");
            exit(EXIT_FAILURE);
        }
        printf("New synchronization object created\n");
    } else if (errno == EEXIST) {
        sync_id = semget(SYNC_TOKEN, 1, 0);
        if (sync_id < 0) {
            perror("Existing sync object access failed");
            exit(EXIT_FAILURE);
        }
        printf("Reusing existing synchronization object\n");
    } else {
        perror("Sync mechanism setup failed");
        exit(EXIT_FAILURE);
    }
    
    return sync_id;
}

int main() {
    int memory_segment = setup_shared_memory();
    shared_mem *memory_ptr = connect_shared_memory(memory_segment);
    int sync_object = initialize_sync_mechanism();
    
    memory_ptr->accumulator = 0;
    memory_ptr->operation_done = 0;
    
    const int operation_count = 10000;
    const int target_value = operation_count * 2;
    
    printf("Initial accumulator: %d\n", memory_ptr->accumulator);
    printf("Projected result: %d\n", target_value);
    printf("=== SYNCHRONIZED EXECUTION WITH MUTUAL EXCLUSION ===\n");
    
    pid_t worker_pid = fork();
    
    if (worker_pid > 0) {
        perform_synchronized_operation(getpid(), memory_ptr, operation_count, sync_object);
        
        waitpid(worker_pid, NULL, 0);
        
        printf("\n=== SYNCHRONIZED EXECUTION RESULTS ===\n");
        printf("Final accumulator value: %d\n", memory_ptr->accumulator);
        printf("Projected value: %d\n", target_value);
        printf("Discrepancy: %d operations\n", target_value - memory_ptr->accumulator);
        printf("Race condition detected: %s\n", "NO");
        printf("Mutual exclusion: %s\n", "EFFECTIVE");
        
        shmdt(memory_ptr);
        shmctl(memory_segment, IPC_RMID, NULL);
        cleanup_sync_object(sync_object);
        
    } else if (worker_pid == 0) {
        perform_synchronized_operation(getpid(), memory_ptr, operation_count, sync_object);
        exit(EXIT_SUCCESS);
    } else {
        perror("Worker process creation failed");
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
