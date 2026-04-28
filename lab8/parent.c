#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_CHLD_PROC_NUM 5 // Определяем количество дочерних процессов

int main(int argc, char **argv) {
    int i = 0;
    pid_t pid[MAX_CHLD_PROC_NUM];

    for (i = 0; i < MAX_CHLD_PROC_NUM; i++) {
        // fork() пишется с маленькой буквы
        if ((pid[i] = fork()) == 0) {
            // Аргументы для execve: путь, массив аргументов, массив окружения
            char *args[] = {"./child", NULL};
            char *env[] = {NULL};
            execve("./child", args, env);
            
            // Если execve вернул управление, значит произошла ошибка
            perror("execve failed");
            exit(1);
        }
    }

    for (;;) {
        printf("родительский процесс: PID = %d sleep.\n", getpid());
        sleep(10);
        printf("родительский процесс: PID = %d wakeup.\n", getpid());
    }

    return 0;
}
