#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void proc_child(void);
void proc_parent(void);

int main(int argc, char **argv) {
    pid_t pid = fork();
    
    // Дочерний процесс, PID равен 0
    if (pid == 0) {
        printf("child process: PID = %d\n", getpid());
        proc_child();
        exit(0);
    }
    // Родительский процесс, PID > 0
    else if (pid > 0) {
        printf("parent process: PID = %d\n", getpid());
        proc_parent();
        exit(0);
    }
    else {
        // Ошибка при вызове fork()
        perror("fork failed");
        exit(1);
    }
}

// Реализация функций (примерная)
void proc_child(void) {
    printf("Child process is working...\n");
    sleep(1);
}

void proc_parent(void) {
    printf("Parent process is working...\n");
    // Ожидание завершения дочернего процесса
    wait(NULL);
    printf("Child process finished, parent continues...\n");
    sleep(1);
}
