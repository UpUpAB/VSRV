#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

int main() {
    int fd[2];
    pid_t pid;
    char message[100];
    
    // Создаем неименованный канал
    if (pipe(fd) == -1) {
        perror("pipe");
        return 1;
    }
    
    pid = fork();
    
    if (pid == 0) {
        // Дочерний процесс - отправитель сообщений
        close(fd[0]); // закрываем конец для чтения
        
        char *messages[] = {"Hello from child!", "How are you?", "This works great!", "END"};
        
        for (int i = 0; i < 4; i++) {
            printf("[SENDER] Sending: '%s'\n", messages[i]);
            write(fd[1], messages[i], strlen(messages[i]) + 1);
            sleep(1); // пауза между сообщениями для наглядности
        }
        
        close(fd[1]);
        printf("[SENDER] All messages sent\n");
    } else {
        // Родительский процесс - получатель сообщений
        close(fd[1]); // закрываем конец для записи
        
        printf("[RECEIVER] Waiting for messages...\n");
        
        while (1) {
            int bytes = read(fd[0], message, sizeof(message));
            if (bytes <= 0) break;
            
            printf("[RECEIVER] Received: '%s'\n", message);
            
            // Проверяем сигнал завершения
            if (strcmp(message, "END") == 0) {
                printf("[RECEIVER] Termination signal received\n");
                break;
            }
        }
        
        close(fd[0]);
        wait(NULL); // ожидаем завершения дочернего процесса
        printf("[RECEIVER] All messages processed\n");
    }
    
    return 0;
}

