
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define MEMORY_KEY 0xABCDE

typedef struct {
    int value;
    int completion_flag;
} shared_data;

void modify_shared_value(pid_t process_id, shared_data *data_ptr, int operations) {
    int temp_value;
    
    fprintf(stdout, "Process %d: beginning operations\n", process_id);
    
    for (int j = 0; j < operations; j++) {
        temp_value = data_ptr->value;
        temp_value++;
        data_ptr->value = temp_value;
        
        if (j % 1000 == 0) {
            usleep(1000);
        }
    }
    
    fprintf(stdout, "Process %d: operations completed\n", process_id);
}

int initialize_shared_memory() {
    int memory_id = shmget(MEMORY_KEY, sizeof(shared_data), 0644|IPC_CREAT);
    if (memory_id < 0) {
        perror("Shared memory creation failed");
        exit(EXIT_FAILURE);
    }
    return memory_id;
}

shared_data* attach_shared_memory(int memory_id) {
    shared_data *data_ptr = shmat(memory_id, NULL, 0);
    if (data_ptr == (void *) -1) {
        perror("Memory attachment failed");
        exit(EXIT_FAILURE);
    }
    return data_ptr;
}
int main() {
    int memory_id = initialize_shared_memory();
    shared_data *data_ptr = attach_shared_memory(memory_id);
    
    data_ptr->value = 0;
    data_ptr->completion_flag = 0;
    
    int operation_count = 10000;
    int expected_result = operation_count * 2;
    
    printf("Starting value: %d\n", data_ptr->value);
    printf("Target value: %d\n", expected_result);
    printf("=== CONCURRENT EXECUTION WITHOUT SYNCHRONIZATION ===\n");
    
    pid_t child_pid = fork();
    
    if (child_pid > 0) {
        modify_shared_value(getpid(), data_ptr, operation_count);
        
        waitpid(child_pid, NULL, 0);
        
        printf("\n=== EXECUTION RESULTS ===\n");
        printf("Final value: %d\n", data_ptr->value);
        printf("Expected value: %d\n", expected_result);
        printf("Lost operations: %d\n", expected_result - data_ptr->value);
        printf("Race condition present: %s\n",
               data_ptr->value != expected_result ? "YES" : "NO");
        
        shmdt(data_ptr);
        shmctl(memory_id, IPC_RMID, NULL);
        
    } else if (child_pid == 0) {
        modify_shared_value(getpid(), data_ptr, operation_count);
        exit(EXIT_SUCCESS);
    } else {
        perror("Process creation failed");
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
