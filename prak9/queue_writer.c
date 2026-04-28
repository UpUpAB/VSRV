// Структура сообщения

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_TEXT 512
#define MSG_KEY 0x1234

// Структура сообщения
struct message {
    long msg_type;
    char msg_text[MAX_TEXT];
};
// Структура сообщения

int main() {
    int msg_id;
    struct message data;
    char buffer[MAX_TEXT];
    
    // Создаем очередь сообщений
    msg_id = msgget(MSG_KEY, 0666 | IPC_CREAT);
    if (msg_id == -1) {
        perror("msgget failed");
        exit(1);
    }
    
    printf("Message sender started. Type messages (enter 'exit' to quit):\n");
    
    while (1) {
        printf("Send > ");
        
        // Читаем ввод пользователя
        if (fgets(buffer, MAX_TEXT, stdin) == NULL) {
            break;
        }
        
        // Устанавливаем тип сообщения и копируем текст
        data.msg_type = 1;
        strncpy(data.msg_text, buffer, MAX_TEXT - 1);
        data.msg_text[MAX_TEXT - 1] = '\0'; // Гарантируем нулевое завершение
        
        // Отправляем сообщение в очередь
        if (msgsnd(msg_id, &data, strlen(data.msg_text) + 1, 0) == -1) {
            perror("msgsnd failed");
            exit(1);
        }
        
        // Проверяем команду выхода
        if (strncmp(buffer, "exit", 4) == 0) {
            break;
        }
    }
    
    printf("Sender finished. Messages sent to queue.\n");
    return 0;
}

