#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define NUM_CHILDREN 10

// Обработчик сигнала SIGCHLD
void child_exit_handler(int sig) {
    pid_t pid;
    int status;

    printf("[Обработчик] Сигнал получен\n");

    // Используем WNOHANG, чтобы не блокировать родителя,
    // и цикл while, чтобы собрать всех детей, завершившихся одновременно
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            printf("   Обработан процесс %d (код: %d)\n",
                   pid, WEXITSTATUS(status));
        }
    }
}

int main() {
    // Регистрация обработчика сигнала завершения дочернего процесса
    signal(SIGCHLD, child_exit_handler);

    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (fork() == 0) {
            // Код дочернего процесса
            sleep(1); // Все завершатся примерно в одно время
            exit(i);
        }
    }

    // Родитель продолжает работу, не блокируясь вызовом wait
    printf("Родитель начинает ожидание...\n");
    
    for (int i = 0; i < 5; i++) {
        printf("Родитель работает... %d/5\n", i + 1);
        sleep(1);
    }

    printf("Родитель завершает работу\n");
    return 0;
}
