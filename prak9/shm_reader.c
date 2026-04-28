// Получаем доступ к существующему сегменту разделяемой памяти

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SHM_SIZE 1024
#define SHM_KEY 0x5678

// Получаем доступ к существующему сегменту разделяемой памяти


int main() {
    int shmid;
    char *shared_memory;
    
    shmid = shmget(SHM_KEY, SHM_SIZE, 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }
    
    shared_memory = shmat(shmid, NULL, 0);
    if (shared_memory == (char *)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    printf("Reader connected to shared memory. Waiting for messages...\n");
    
    while (1) {
        while (shared_memory[SHM_SIZE-1] != '1') {
            usleep(100000); /
        }
        
        if (strncmp(shared_memory, "exit", 4) == 0) {
            break;
        }
        
        printf("Received: %s", shared_memory);
        
        shared_memory[SHM_SIZE-1] = '0';
    }
    
    if (shmdt(shared_memory) == -1) {
        perror("shmdt failed");
    }
    
    printf("Reader finished work.\n");
    return 0;
}

