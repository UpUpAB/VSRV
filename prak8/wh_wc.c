#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

int main() {
    int fd[2];
    pid_t pid1, pid2;
    
    // Создаем канал
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(1);
    }
    
    // Первый процесс - who
    pid1 = fork();
    if (pid1 == 0) {
        // Дочерний процесс для who
        close(fd[0]); // закрываем чтение
        dup2(fd[1], STDOUT_FILENO); // перенаправляем stdout в канал
        close(fd[1]);
        
        execlp("who", "who", NULL);
        perror("execlp who");
        exit(1);
    }
    
    // Второй процесс - wc -l
    pid2 = fork();
    if (pid2 == 0) {
        // Дочерний процесс для wc -l
        close(fd[1]); // закрываем запись
        dup2(fd[0], STDIN_FILENO); // перенаправляем stdin из канала
        close(fd[0]);
        
        execlp("wc", "wc", "-l", NULL);
        perror("execlp wc");
        exit(1);
    }
    
    // Родительский процесс закрывает оба конца канала
    close(fd[0]);
    close(fd[1]);
    
    // Ждем завершения обоих дочерних процессов
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    
    return 0;
}

