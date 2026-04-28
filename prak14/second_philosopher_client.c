#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h> // Для безопасного приведения типов указателей

#define PHILO 5
#define PORT 8080
#define SERVER_IP "127.0.0.1"
#define FOOD 50
#define DELAY 50000

pthread_mutex_t foodlock;
static int food_count = FOOD;

// Глобальная функция для имитации потребления еды
int food_on_table() {
  int myfood;
  pthread_mutex_lock(&foodlock);
  if (food_count > 0) {
    food_count--;
  }
  myfood = food_count;
  pthread_mutex_unlock(&foodlock);
  return myfood;
}

void *philosopher (void *num) {
    // Безопасное приведение void* к int
    int id = (int)(intptr_t)num;
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char message[32];
    int f;
    
    printf("Philosopher %d is reflecting...\n", id);

    // 1. Создание сокета и подключение
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Socket creation error \n");
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("\nInvalid address/ Address not supported \n");
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("\nConnection Failed \n");
        return NULL;
    }
    printf("Philosopher %d connected to Manager.\n", id);

    // Цикл: пока есть еда
    while ((f = food_on_table())) { // Используем скобки, чтобы избежать предупреждения
        
        // Думаем некоторое время
        usleep(rand() % 100000 + 100000);

        // 3. Запрос вилок у Сервера
        printf("Philosopher %d: get dish %d. REQUESTING forks.\n", id, f);
        sprintf(message, "REQUEST %d", id);
        
        // Отправляем запрос
        send(sock, message, strlen(message), 0);
        
        // **БЛОКИРОВКА:** Ждем ответа GRANTED.
        // Сервер блокирует поток, пока вилки не будут доступны.
        int read_size = recv(sock, buffer, 1024, 0);
        buffer[read_size] = '\0';
        
        if (strcmp(buffer, "GRANTED") == 0) {
            printf("Philosopher %d: EATING (Got GRANTED).\n", id);
        } else {
            // Если мы не получили GRANTED, произошла ошибка протокола
            printf("Philosopher %d: ERROR: Expected GRANTED but got %s.\n", id, buffer);
            close(sock);
            return NULL;
        }

        // 4. Еда
        usleep(DELAY * (FOOD - f + 1));
        
        // 5. Освобождение вилок
        sprintf(message, "RELEASE %d", id);
        send(sock, message, strlen(message), 0);
        
        // Ждем ACK
        recv(sock, buffer, 1024, 0);
    }
    
    printf("Philosopher %d is done eating.\n", id);
    close(sock);
    return NULL;
}

int main() {
    pthread_t phils[PHILO];
    pthread_mutex_init(&foodlock, NULL);
    srand(time(NULL));

    for (int i = 0; i < PHILO; i++)
        // Безопасное приведение int к void*
        pthread_create(&phils[i], NULL, philosopher, (void *)(intptr_t)i);
        
    for (int i = 0; i < PHILO; i++)
        pthread_join(phils[i], NULL);
        
    return 0;
}
