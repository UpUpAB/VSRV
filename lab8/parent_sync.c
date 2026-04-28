#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_CHLD_PROC_NUM 10

// Прототип функции-обработчика
void child_exit_handler(int sig);

int main(int argc, char **argv) {
    pid_t pid[MAX_CHLD_PROC_NUM];

    // Регистрация обработчика сигнала SIGCHLD в ядре ОС
    // Используем signal() вместо Signal()
    signal(SIGCHLD, child_exit_handler);

    // Создаём 10 дочерних процессов
    for (int i = 0; i < MAX_CHLD_PROC_NUM; i++) {
        // Используем fork() вместо Fork()
        if ((pid[i] = fork()) == 0) {
            // Дочерний процесс
            execve("./child", NULL, NULL);
            
            // Если execve вернулся — ошибка
            perror("execve failed");
            exit(1);
        } else if (pid[i] < 0) {
            perror("fork failed");
            exit(1);
        }
    }

    printf("Родительский процесс создал %d дочерних процессов\n", MAX_CHLD_PROC_NUM);
    printf("Родитель продолжает работу в фоновом режиме...\n");

    // Родительский процесс продолжает свою работу
    int counter = 0;
    while (1) {
        printf("Родительский процесс выполняет свою работу... (итерация %d)\n", ++counter);
        sleep(2);
    }

    exit(0);
}

// Обработчик сигнала SIGCHLD
void child_exit_handler(int sig) {
    int status;
    pid_t child_pid;

    // Важно: используем WNOHANG, чтобы не блокироваться
    // и собираем ВСЕ завершившиеся процессы в цикле
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            printf("[Обработчик] Дочерний процесс %d завершился с кодом %d\n",
                   child_pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[Обработчик] Дочерний процесс %d убит сигналом %d\n",
                   child_pid, WTERMSIG(status));
        } else if (WIFSTOPPED(status)) {
            printf("[Обработчик] Дочерний процесс %d остановлен сигналом %d\n",
                   child_pid, WSTOPSIG(status));
        }
    }

    // Восстанавливаем обработчик (некоторые системы сбрасывают его после вызова)
    signal(SIGCHLD, child_exit_handler);
}
