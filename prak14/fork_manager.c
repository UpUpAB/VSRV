
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
pthread_mutex_t state_lock;

void init_forks() {
    for (int i = 0; i < PHILO; i++) {
        fork_state[i] = FORK_FREE;
    }
    pthread_mutex_init(&state_lock, NULL);
}

// Попытка взять обе вилки
int try_get_forks(int phil_id) {
    int fork_r = phil_id;
    int fork_l = (phil_id + 1) % PHILO;
    int granted = 0;

    pthread_mutex_lock(&state_lock);
    
    // Проверка, свободны ли обе вилки (Fork_r и Fork_l)
    if (fork_state[fork_r] == FORK_FREE && fork_state[fork_l] == FORK_FREE) {
        fork_state[fork_r] = phil_id;
        fork_state[fork_l] = phil_id;
        granted = 1;
        printf("[Manager]: Philosopher %d GRANTED forks %d and %d.\n", phil_id, fork_r, fork_l);
    } else {
        printf("[Manager]: Philosopher %d DENIED (f%d=%d, f%d=%d).\n",
               phil_id, fork_r, fork_state[fork_r], fork_l, fork_state[fork_l]);
    }
    
    pthread_mutex_unlock(&state_lock);
    return granted;
}

// Освобождение обеих вилок
void release_forks(int phil_id) {
    int fork_r = phil_id;
    int fork_l = (phil_id + 1) % PHILO;

    pthread_mutex_lock(&state_lock);
    
    fork_state[fork_r] = FORK_FREE;
    fork_state[fork_l] = FORK_FREE;
    printf("[Manager]: Philosopher %d RELEASED forks %d and %d.\n", phil_id, fork_r, fork_l);
    
    pthread_mutex_unlock(&state_lock);
}

void *handle_client(void *socket_desc) {
    int new_socket = *(int*)socket_desc;
    char buffer[1024] = {0};
    int read_size;
    
    // Получение сообщений от клиента
    while ((read_size = recv(new_socket, buffer, 1024, 0)) > 0) {
        buffer[read_size] = '\0';
        char cmd[10];
        int phil_id;
        
        // Разбор команды (e.g., "REQUEST 3" or "RELEASE 3")
        if (sscanf(buffer, "%s %d", cmd, &phil_id) == 2) {
            
            if (strcmp(cmd, "REQUEST") == 0) {
                if (try_get_forks(phil_id)) {
                    send(new_socket, "GRANTED", 7, 0);
                } else {
                    send(new_socket, "DENIED", 6, 0);
                }
            }
            else if (strcmp(cmd, "RELEASE") == 0) {
                release_forks(phil_id);
                send(new_socket, "ACK", 3, 0);
            }
        }
        memset(buffer, 0, 1024); // Очистка буфера
    }

    if (read_size == 0) {
        puts("Client disconnected");
    } else if (read_size == -1) {
        perror("recv failed");
    }

    free(socket_desc); // Освобождаем память для дескриптора сокета
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
    server.sin_addr.s_addr = INADDR_ANY; // Принимать соединения с любого IP
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
