
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PHILO 5
#define PORT 8080
#define SERVER_IP "127.0.0.1"
#define FOOD 50
#define DELAY 50000 // Задержка в микросекундах для еды

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
    int id = (int)num;
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    char message[32];
    int f;
    
    printf("Philosopher %d is reflecting...\n", id);

    // 1. Создание сокета
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("\n Socket creation error \n");
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Преобразование адреса IPv4 и подключение
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("\nInvalid address/ Address not supported \n");
        return NULL;
    }

    // 2. Подключение к серверу ForkManager
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("\nConnection Failed \n");
        return NULL;
    }
    printf("Philosopher %d connected to Manager.\n", id);

    while (f = food_on_table()) {
        
        // Думаем некоторое время
        usleep(rand() % 100000 + 100000);

        // 3. Запрос вилок у Сервера
        printf("Philosopher %d: get dish %d. REQUESTING forks.\n", id, f);
        sprintf(message, "REQUEST %d", id);
        
        // Отправляем запрос, пока не получим GRANTED
        int granted = 0;
        while (!granted) {
            send(sock, message, strlen(message), 0);
            
            // Ждем ответа
            int read_size = recv(sock, buffer, 1024, 0);
            buffer[read_size] = '\0';
            
            if (strcmp(buffer, "GRANTED") == 0) {
                printf("Philosopher %d: EATING (Got GRANTED).\n", id);
                granted = 1;
            } else {
                // Если DENIED, ждем немного и пробуем снова
                usleep(rand() % 50000);
                printf("Philosopher %d: DENIED. Retrying...\n", id);
            }
            memset(buffer, 0, 1024);
        }

        // 4. Еда
        usleep(DELAY * (FOOD - f + 1));
        
        // 5. Освобождение вилок
        sprintf(message, "RELEASE %d", id);
        send(sock, message, strlen(message), 0);
        
        // Ждем ACK (необязательно, но полезно для синхронизации)
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

    // Запуск всех 5 процессов-философов
    for (int i = 0; i < PHILO; i++)
        pthread_create(&phils[i], NULL, philosopher, (void *)i);
        
    for (int i = 0; i < PHILO; i++)
        pthread_join(phils[i], NULL);
        
    return 0;
}
