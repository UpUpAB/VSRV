#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_TEXT 512
#define MSG_KEY 0x1234

// Структура сообщения (должна совпадать с отправителем)
struct message {
    long msg_type;
    char msg_text[MAX_TEXT];
};

int main() {
    int msg_id;
    struct message data;
    int running = 1;
    
    // Получаем доступ к очереди сообщений
    msg_id = msgget(MSG_KEY, 0666);
    if (msg_id == -1) {
        perror("msgget failed");
        exit(1);
    }
    
    printf("Message receiver started. Waiting for messages...\n");
    
    while (running) {
        // Получаем сообщение из очереди (блокирующий вызов)
        if (msgrcv(msg_id, &data, MAX_TEXT, 1, 0) == -1) {
            perror("msgrcv failed");
            exit(1);
        }
        
        printf("Received: %s", data.msg_text);
        
        // Проверяем команду выхода
        if (strncmp(data.msg_text, "exit", 4) == 0) {
            running = 0;
        }
    }
    
    // Удаляем очередь сообщений
    if (msgctl(msg_id, IPC_RMID, 0) == -1) {
        perror("msgctl failed");
        exit(1);
    }
    
    printf("Receiver finished. Message queue deleted.\n");
    return 0;
}

