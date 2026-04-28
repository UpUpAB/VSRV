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
#define FORK_FREE -1
#define MAX_CLIENTS 10

// Состояние вилок: -1 = Свободна; 0..4 = ID философа, который ее держит.
int fork_state[PHILO];
pthread_mutex_t state_lock;   // Мьютекс для защиты состояния fork_state
pthread_cond_t forks_cond;    // Условная переменная для ожидания свободных вилок

void init_forks() {
    for (int i = 0; i < PHILO; i++) {
        fork_state[i] = FORK_FREE;
    }
    pthread_mutex_init(&state_lock, NULL);
    pthread_cond_init(&forks_cond, NULL); // Инициализация условной переменной
}

/**
 * Атомарный захват вилок.
 * Поток блокируется, пока обе вилки не станут свободны.
 */
int get_forks_atomically(int phil_id) {
    int fork_r = phil_id;
    int fork_l = (phil_id + 1) % PHILO;

    // 1. Захватываем мьютекс, защищающий состояние
    pthread_mutex_lock(&state_lock);

    // 2. Условие ожидания (в цикле WHILE для избежания "потерянного пробуждения")
    while (fork_state[fork_r] != FORK_FREE || fork_state[fork_l] != FORK_FREE) {
        printf("[Manager]: Philosopher %d WAITING for forks %d and %d.\n", phil_id, fork_r, fork_l);
        
        // Атомарно освобождает state_lock И блокирует поток, ожидая сигнал.
        // Когда поток пробуждается, он автоматически повторно захватывает state_lock
        pthread_cond_wait(&forks_cond, &state_lock);
    }

    // 3. Условие выполнено: захват ресурсов
    fork_state[fork_r] = phil_id;
    fork_state[fork_l] = phil_id;
    
    printf("[Manager]: Philosopher %d GRANTED forks %d and %d.\n", phil_id, fork_r, fork_l);

    // 4. Освобождаем мьютекс
    pthread_mutex_unlock(&state_lock);
    return 1;
}

/**
 * Освобождение обеих вилок и оповещение ожидающих потоков.
 */
void release_forks(int phil_id) {
    int fork_r = phil_id;
    int fork_l = (phil_id + 1) % PHILO;

    pthread_mutex_lock(&state_lock);
    
    // Освобождение
    fork_state[fork_r] = FORK_FREE;
    fork_state[fork_l] = FORK_FREE;
    printf("[Manager]: Philosopher %d RELEASED forks %d and %d. Signaling waiters.\n", phil_id, fork_r, fork_l);
    
    // Оповещаем ВСЕХ ожидающих философов, чтобы они проверили условие
    pthread_cond_broadcast(&forks_cond);
    
    pthread_mutex_unlock(&state_lock);
}

void *handle_client(void *socket_desc) {
    int new_socket = *(int*)socket_desc;
    char buffer[1024] = {0};
    int read_size;
    
    while ((read_size = recv(new_socket, buffer, 1024, 0)) > 0) {
        buffer[read_size] = '\0';
        char cmd[10];
        int phil_id;
        
        if (sscanf(buffer, "%s %d", cmd, &phil_id) == 2) {
            
            if (strcmp(cmd, "REQUEST") == 0) {
                // Вызываем блокирующую функцию
                get_forks_atomically(phil_id);
                // Ответ GRANTED отправляется только после успешного захвата
                send(new_socket, "GRANTED", 7, 0);
            }
            else if (strcmp(cmd, "RELEASE") == 0) {
                release_forks(phil_id);
                send(new_socket, "ACK", 3, 0);
            }
        }
        memset(buffer, 0, 1024);
    }

    if (read_size == 0) {
        puts("Client disconnected");
    } else if (read_size == -1) {
        perror("recv failed");
    }

    free(socket_desc);
    close(new_socket);
    return NULL;
}

int main() {
    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;
    
    init_forks();

    // Создание сокета
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        perror("Could not create socket");
        return 1;
    }
    
    // Настройка адреса
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);
    
    // Привязка
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        return 1;
    }
    puts("[Manager]: Bind done. Listening for philosophers...");
    
    // Прослушивание
    listen(socket_desc, MAX_CLIENTS);
    
    c = sizeof(struct sockaddr_in);
    
    // Принятие входящих соединений и создание потоков для каждого философа
    while ((client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c))) {
        puts("[Manager]: New connection accepted.");
        
        pthread_t thread_id;
        int *new_sock = (int*)malloc(sizeof(int));
        *new_sock = client_sock;
        
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
        }
    }
    
    if (client_sock < 0) {
        perror("Accept failed");
        return 1;
    }

    return 0;
}
