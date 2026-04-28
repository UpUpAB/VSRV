// Создаем сегмент разделяемой памяти
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SHM_SIZE 1024
#define SHM_KEY 0x5678

int main() {
    int shmid;
    char *shared_memory;
    char buffer[SHM_SIZE];
    
    shmid = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }
    
    shared_memory = shmat(shmid, NULL, 0);
    if (shared_memory == (char *)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    printf("Writer connected to shared memory. Enter messages:\n");
    
    shared_memory[SHM_SIZE-1] = '0';
    
    while (1) {
        printf("> ");
        fgets(buffer, SHM_SIZE, stdin);
        
        strncpy(shared_memory, buffer, SHM_SIZE-1);
        
        shared_memory[SHM_SIZE-1] = '1';
        
        if (strncmp(buffer, "exit", 4) == 0) {
            break;
        }
        
        printf("Waiting for reader...\n");
        while (shared_memory[SHM_SIZE-1] == '1') {
            sleep(1);
        }
    }
    
    if (shmdt(shared_memory) == -1) {
        perror("shmdt failed");
    }
    
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl failed");
    }
    
    printf("Writer finished work. Shared memory deleted.\n");
    return 0;
}

