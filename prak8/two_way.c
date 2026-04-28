#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

int main() {
    int parent_to_child[2]; // канал для сообщений от родителя к ребенку
    int child_to_parent[2]; // канал для сообщений от ребенка к родителю
    pid_t pid;
    char buffer[100];
    
    // Создаем два канала для двусторонней связи
    if (pipe(parent_to_child) == -1 || pipe(child_to_parent) == -1) {
        perror("pipe");
        return 1;
    }
    
    pid = fork();
    
    if (pid == 0) {
        // Дочерний процесс
        close(parent_to_child[1]); // закрываем запись в канал от родителя
        close(child_to_parent[0]); // закрываем чтение из канала к родителю
        
        // Получаем первое сообщение от родителя
        read(parent_to_child[0], buffer, sizeof(buffer));
        printf("[CHILD] Received from parent: '%s'\n", buffer);
        
        // Отправляем ответ родителю
        char *response = "Thank you for message! I'm doing well!";
        write(child_to_parent[1], response, strlen(response) + 1);
        printf("[CHILD] Sending to parent: '%s'\n", response);
        
        // Получаем второе сообщение от родителя
        read(parent_to_child[0], buffer, sizeof(buffer));
        printf("[CHILD] Received from parent: '%s'\n", buffer);
        
        // Отправляем финальное сообщение
        char *final_msg = "Goodbye! Have a nice day!";
        write(child_to_parent[1], final_msg, strlen(final_msg) + 1);
        printf("[CHILD] Sending to parent: '%s'\n", final_msg);
        
        close(parent_to_child[0]);
        close(child_to_parent[1]);
    } else {
        // Родительский процесс
        close(parent_to_child[0]); // закрываем чтение из канала к ребенку
        close(child_to_parent[1]); // закрываем запись в канал от ребенка
        
        // Отправляем первое сообщение ребенку
        char *message1 = "Hello child! How are you doing?";
        write(parent_to_child[1], message1, strlen(message1) + 1);
        printf("[PARENT] Sending to child: '%s'\n", message1);
        
        // Получаем ответ от ребенка
        read(child_to_parent[0], buffer, sizeof(buffer));
        printf("[PARENT] Received from child: '%s'\n", buffer);
        
        // Отправляем второе сообщение
        char *message2 = "Glad to hear that! Have a great day!";
        write(parent_to_child[1], message2, strlen(message2) + 1);
        printf("[PARENT] Sending to child: '%s'\n", message2);
        
        // Получаем финальное сообщение
        read(child_to_parent[0], buffer, sizeof(buffer));
        printf("[PARENT] Received from child: '%s'\n", buffer);
        
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        wait(NULL); // ожидаем завершения дочернего процесса
        printf("[PARENT] Conversation completed\n");
    }
    
    return 0;
}

