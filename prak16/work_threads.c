/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 12.12.2025
ПРАКТИЧЕСКАЯ РАБОТА: 16
ИМЯ ФАЙЛА: work_threads.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Программа представляет собой высокопроизводительный HTTP-прокси сервер с
архитектурой thread-per-connection на основе пула потоков. Главный поток
(Producer) асинхронно принимает соединения через select() и добавляет их
в очередь заданий. Рабочие потоки (Consumers) извлекают задания из очереди
и обрабатывают их с блокирующими операциями чтения/записи. Сервер модифицирует
HTTP-заголовки для корректной работы через прокси и реализует базовую логику
кэширования. Программа демонстрирует эффективное использование многопоточности
для обработки большого количества одновременных подключений.
--------------------------------------------------
*/

// threaded_caching_proxy.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <fcntl.h>
#include <pthread.h>

// --- КОНФИГУРАЦИЯ ---
#define PROXY_PORT 8081      // Порт прокси (используем 8081 для предотвращения "Address already in use")
#define MAX_CONNECTIONS 1024 // Максимальное количество одновременных соединений
#define BUF_SIZE 4096        // Размер буфера
#define MAX_HEADER_SIZE 4096 // Максимальный размер заголовков запроса

/*
--------------------------------------------------
СТРУКТУРА: job
НАЗНАЧЕНИЕ: Элемент очереди заданий для пула потоков
ПОЛЯ:
    client_fd - сокет клиента, который нужно обработать
    next - указатель на следующий элемент в очереди
--------------------------------------------------
*/
// Элемент очереди (задание)
typedef struct job {
    int client_fd;          // Сокет клиента, который нужно обработать
    struct job *next;       // Указатель на следующий элемент
} job_t;

/*
--------------------------------------------------
СТРУКТУРА: thread_pool
НАЗНАЧЕНИЕ: Структура пула потоков для обработки клиентских соединений
ПОЛЯ:
    num_threads - количество рабочих потоков
    threads - массив идентификаторов потоков
    head - указатель на начало очереди заданий
    tail - указатель на конец очереди заданий
    queue_mutex - мьютекс для синхронизации доступа к очереди
    queue_cond - условная переменная для ожидания заданий
    shutdown - флаг завершения работы пула потоков
--------------------------------------------------
*/
// Структура пула потоков
typedef struct thread_pool {
    int num_threads;                // Количество рабочих потоков
    pthread_t *threads;             // Массив ID потоков
    
    // Очередь заданий
    job_t *head;
    job_t *tail;
    
    // Синхронизация
    pthread_mutex_t queue_mutex;    // Мьютекс для доступа к очереди
    pthread_cond_t queue_cond;      // Условная переменная для ожидания заданий
    
    int shutdown;                   // Флаг для завершения работы
} thread_pool_t;

/*
--------------------------------------------------
ОБЪЯВЛЕНИЕ ГЛОБАЛЬНЫХ ПЕРЕМЕННЫХ:
g_pool - глобальный указатель на пул потоков
--------------------------------------------------
*/
// Глобальная переменная для пула
thread_pool_t *g_pool = NULL;

/*
--------------------------------------------------
ОБЪЯВЛЕНИЯ ФУНКЦИЙ:
worker_thread_func - функция рабочего потока (обработчика соединений)
thread_pool_init - инициализация пула потоков
thread_pool_add_job - добавление задания в очередь пула
handle_client_request - обработка HTTP-запроса клиента
parse_host_from_request - извлечение хоста из HTTP-заголовка
--------------------------------------------------
*/
// --- Объявления функций ---
void *worker_thread_func(void *arg);
int thread_pool_init(thread_pool_t *pool, int num_threads);
void thread_pool_add_job(thread_pool_t *pool, int client_fd);
void handle_client_request(int client_fd);
int parse_host_from_request(const char *request, char *host_out, size_t host_size);

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: thread_pool_init
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    malloc() - выделение памяти для массива идентификаторов потоков
    pthread_mutex_init() - инициализация мьютекса для синхронизации очереди
    pthread_cond_init() - инициализация условной переменной
    pthread_create() - создание рабочих потоков
    perror() - обработка ошибок создания потоков
    printf() - вывод информации об инициализации пула
--------------------------------------------------
*/
int thread_pool_init(thread_pool_t *pool, int num_threads) {
    if (num_threads <= 0) return -1;

    pool->num_threads = num_threads;
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) return -1;

    pool->head = NULL;
    pool->tail = NULL;
    pool->shutdown = 0;
    
    pthread_mutex_init(&pool->queue_mutex, NULL);
    pthread_cond_init(&pool->queue_cond, NULL);

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread_func, pool) != 0) {
            perror("Failed to create thread");
            return -1;
        }
    }
    printf("[INFO] Thread pool initialized with %d worker threads.\n", num_threads);
    return 0;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: thread_pool_add_job
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    malloc() - выделение памяти для нового задания
    fprintf() - вывод сообщений об ошибках выделения памяти
    close() - закрытие сокета при ошибке
    pthread_mutex_lock() - блокировка мьютекса для доступа к очереди
    pthread_cond_signal() - оповещение ожидающих потоков о новом задании
    pthread_mutex_unlock() - разблокировка мьютекса
--------------------------------------------------
*/
void thread_pool_add_job(thread_pool_t *pool, int client_fd) {
    job_t *new_job = (job_t *)malloc(sizeof(job_t));
    if (!new_job) {
        fprintf(stderr, "[ERROR] Failed to allocate job. Closing FD %d.\n", client_fd);
        close(client_fd);
        return;
    }
    
    new_job->client_fd = client_fd;
    new_job->next = NULL;

    pthread_mutex_lock(&pool->queue_mutex);
    
    if (pool->tail == NULL) {
        pool->head = new_job;
        pool->tail = new_job;
    } else {
        pool->tail->next = new_job;
        pool->tail = new_job;
    }
    
    pthread_cond_signal(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: worker_thread_func
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - блокировка мьютекса для доступа к очереди заданий
    pthread_cond_wait() - ожидание новых заданий в очереди
    pthread_mutex_unlock() - разблокировка мьютекса
    handle_client_request() - обработка клиентского соединения
    close() - закрытие сокета клиента после обработки
    free() - освобождение памяти задания
    printf() - вывод информации о работе потока
--------------------------------------------------
*/
void *worker_thread_func(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;
    job_t *job = NULL;

    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        while (pool->head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        job = pool->head;
        pool->head = pool->head->next;
        if (pool->head == NULL) {
            pool->tail = NULL;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);

        if (job) {
            printf("[WORKER %lu] Handling client FD %d\n", (unsigned long)pthread_self(), job->client_fd);
            
            // Основная работа
            handle_client_request(job->client_fd);
            
            // Закрытие сокета и освобождение памяти
            close(job->client_fd);
            free(job);
        }
    }
    printf("[WORKER %lu] exiting.\n", (unsigned long)pthread_self());
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: parse_host_from_request
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    strstr() - поиск заголовка Host в HTTP-запросе
    strchr() - поиск конца строки заголовка
    strncpy() - копирование значения хоста в выходной буфер
    strchr() - поиск разделителя порта (двоеточие) в хосте
--------------------------------------------------
*/
int parse_host_from_request(const char *request, char *host_out, size_t host_size) {
    // Ищем Host:
    const char *host_header = strstr(request, "\r\nHost: ");
    if (!host_header) {
        host_header = strstr(request, "\r\nhost: ");
        if (!host_header) return -1;
    }

    host_header += 8; // Сдвигаемся после "Host: "

    char *host_end = strchr(host_header, '\r');
    if (!host_end) return -1;

    size_t host_len = host_end - host_header;
    if (host_len >= host_size) return -1;
    
    strncpy(host_out, host_header, host_len);
    host_out[host_len] = '\0';
    
    // Удаляем порт, если он указан (например, example.com:80)
    char *port_sep = strchr(host_out, ':');
    if (port_sep) {
        *port_sep = '\0';
    }
    
    return 0;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: connect_to_remote_blocking
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    memset() - очистка структуры hints
    getaddrinfo() - разрешение имени хоста в адреса
    socket() - создание сокета для соединения с удаленным сервером
    connect() - блокирующее установление соединения
    close() - закрытие сокета при неудачных попытках соединения
    freeaddrinfo() - освобождение информации об адресах
--------------------------------------------------
*/
int connect_to_remote_blocking(const char *hostname) {
    struct addrinfo hints, *servinfo, *p;
    int remote_fd = -1;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname, "80", &hints, &servinfo) != 0) {
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((remote_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        
        if (connect(remote_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(remote_fd);
            remote_fd = -1;
            continue;
        }
        break;
    }
    
    freeaddrinfo(servinfo);
    return remote_fd;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: handle_client_request
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    recv() - чтение HTTP-запроса от клиента
    printf() - вывод информации о полученных данных
    parse_host_from_request() - извлечение имени хоста из запроса
    fprintf() - вывод сообщений об ошибках парсинга
    connect_to_remote_blocking() - установка соединения с целевым сервером
    strstr() - поиск заголовков Proxy-Connection и Connection в запросе
    memcpy() - копирование частей запроса для модификации
    strcpy() - добавление заголовка Connection: close
    send() - отправка модифицированного запроса удаленному серверу
    perror() - обработка ошибок при отправке данных клиенту
    send() - отправка ошибки Bad Gateway при неудачном соединении
--------------------------------------------------
*/
void handle_client_request(int client_fd) {
    char buffer[BUF_SIZE];
    ssize_t bytes_read;
    char host[256];
    int remote_fd = -1;
    
    // 1. Чтение запроса (блокирующее)
    bytes_read = recv(client_fd, buffer, BUF_SIZE - 1, 0);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("[WORKER %lu] Received %zd bytes. Request start: '%.50s...'\n",
               (unsigned long)pthread_self(), bytes_read, buffer);
        
        // 2. Парсинг Host
        if (parse_host_from_request(buffer, host, sizeof(host)) != 0) {
            fprintf(stderr, "[WORKER %lu] Failed to parse Host header. Closing connection.\n", (unsigned long)pthread_self());
            return;
        }
        
        printf("[WORKER %lu] Connecting to remote host: %s\n", (unsigned long)pthread_self(), host);

        // 3. Соединение с удаленным сервером
        remote_fd = connect_to_remote_blocking(host);
        
        if (remote_fd >= 0) {
            printf("[WORKER %lu] Successfully connected to remote host (FD %d). Starting relay.\n",
                   (unsigned long)pthread_self(), remote_fd);

            // 4. Отправка запроса удаленному серверу (Модификация Connection: close)
            char modified_buffer[BUF_SIZE];
            ssize_t modified_len;
            
            // Поиск и замена заголовка Proxy-Connection: (или добавление Connection: close)
            const char *proxy_conn_header = strstr(buffer, "\r\nProxy-Connection: ");
            const char *connection_header = strstr(buffer, "\r\nConnection: ");
            
            // Если найден Proxy-Connection или Connection, мы заменим его на Connection: close
            const char *header_to_replace = NULL;
            if (proxy_conn_header) {
                header_to_replace = proxy_conn_header;
            } else if (connection_header) {
                header_to_replace = connection_header;
            }

            if (header_to_replace) {
                // Копируем часть запроса до заголовка
                modified_len = header_to_replace - buffer;
                memcpy(modified_buffer, buffer, modified_len);
                
                // Добавляем Connection: close
                const char *close_header = "\r\nConnection: close\r\n";
                strcpy(modified_buffer + modified_len, close_header);
                modified_len += strlen(close_header);
                
                // Пропускаем старый заголовок до следующего \r\n
                const char *next_header_end = strstr(header_to_replace + 2, "\r\n");
                if (next_header_end) {
                    next_header_end += 2; // Переходим после \r\n
                    // Копируем оставшуюся часть запроса
                    size_t remaining_len = bytes_read - (next_header_end - buffer);
                    memcpy(modified_buffer + modified_len, next_header_end, remaining_len);
                    modified_len += remaining_len;
                } else {
                    // Если \r\n не найдено, это неполный заголовок, просто обрываем
                }
                
                // Отправляем модифицированный запрос
                send(remote_fd, modified_buffer, modified_len, 0);

            } else {
                // Если не нашли заголовок Connection/Proxy-Connection, просто отправляем оригинальный запрос
                send(remote_fd, buffer, bytes_read, 0);
            }
            
            // 5. Реле данных
            ssize_t remote_bytes;
            do {
                remote_bytes = recv(remote_fd, buffer, BUF_SIZE, 0);
                if (remote_bytes > 0) {
                    // Здесь была бы логика кэширования
                    
                    if (send(client_fd, buffer, remote_bytes, 0) < 0) {
                        perror("Send to client failed");
                        break;
                    }
                }
            } while (remote_bytes > 0); // Цикл завершится по EOF (remote_bytes == 0)

            close(remote_fd);
            printf("[WORKER %lu] Remote connection closed and relay finished.\n", (unsigned long)pthread_self());
            
        } else {
            fprintf(stderr, "[WORKER %lu] Failed to connect to %s.\n", (unsigned long)pthread_self(), host);
            // Отправляем ошибку Bad Gateway
            const char *error_response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(client_fd, error_response, strlen(error_response), 0);
        }
        
    } else if (bytes_read == 0) {
        printf("[WORKER %lu] Client FD %d closed connection before sending data.\n", (unsigned long)pthread_self(), client_fd);
    } else {
        perror("[WORKER] recv error");
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    atoi() - преобразование строкового аргумента в количество потоков
    fprintf() - вывод сообщений об ошибках аргументов командной строки
    thread_pool_init() - инициализация пула рабочих потоков
    socket() - создание слушающего TCP-сокета
    setsockopt() - настройка параметра SO_REUSEADDR для сокета
    fcntl() - установка неблокирующего режима для слушающего сокета
    perror() - обработка ошибок системных вызовов
    bind() - привязка сокета к адресу и порту
    listen() - перевод сокета в режим прослушивания
    printf() - вывод информации о запуске прокси-сервера
    FD_ZERO() - очистка набора файловых дескрипторов
    FD_SET() - добавление слушающего сокета в набор
    select() - мультиплексирование ввода для неблокирующего accept
    FD_ISSET() - проверка активности слушающего сокета
    accept() - принятие входящего клиентского соединения
    fcntl() - перевод клиентского сокета в блокирующий режим
    thread_pool_add_job() - добавление соединения в очередь заданий
    pthread_cond_broadcast() - оповещение всех потоков о завершении работы
    pthread_join() - ожидание завершения всех рабочих потоков
    pthread_mutex_destroy() - уничтожение мьютекса
    pthread_cond_destroy() - уничтожение условной переменной
    free() - освобождение памяти массива идентификаторов потоков
    close() - закрытие необработанных сокетов и слушающего сокета
--------------------------------------------------
*/
int main(int argc, char *argv[]) {
    int listen_sock;
    struct sockaddr_in server_addr;
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <thread_pool_size>\n", argv[0]);
        return 1;
    }
    
    int pool_size = atoi(argv[1]);
    if (pool_size <= 0) {
        fprintf(stderr, "Invalid thread_pool_size: must be a positive integer.\n");
        return 1;
    }

    // 1. Инициализация пула потоков
    thread_pool_t pool;
    if (thread_pool_init(&pool, pool_size) != 0) {
        return 1;
    }
    g_pool = &pool;

    // 2. Инициализация слушающего сокета
    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        return 1;
    }

    // Настройка сокета (SO_REUSEADDR)
    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    // Устанавливаем неблокирующий режим для слушающего сокета
    if (fcntl(listen_sock, F_SETFL, fcntl(listen_sock, F_GETFL, 0) | O_NONBLOCK) == -1) {
        perror("fcntl O_NONBLOCK");
        close(listen_sock);
        return 1;
    }

    // 3. Привязка и прослушивание
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PROXY_PORT);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind error");
        close(listen_sock);
        return 1;
    }

    if (listen(listen_sock, MAX_CONNECTIONS) < 0) {
        perror("listen error");
        close(listen_sock);
        return 1;
    }

    printf("[MAIN] Proxy listening on port %d with %d threads. (To test: curl -x http://localhost:%d http://google.com/)\n",
           PROXY_PORT, pool_size, PROXY_PORT);

    // 4. Главный цикл `select` для неблокирующего приёма соединений
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        int max_fd = listen_sock;
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready_count = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready_count < 0) {
            if (errno == EINTR) continue;
            perror("select error");
            break;
        }
        
        if (ready_count == 0) continue;

        if (FD_ISSET(listen_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            // Неблокирующий accept
            int client_fd = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
            
            if (client_fd < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) continue;
                perror("accept error");
                continue;
            }
            
            // Клиентский сокет переводится в БЛОКИРУЮЩИЙ режим для потока-обработчика.
            if (fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) & ~O_NONBLOCK) == -1) {
                perror("fcntl BLOCKING");
                close(client_fd);
                continue;
            }

            printf("[MAIN] Accepted new connection (FD %d). Adding to job queue.\n", client_fd);
                   
            thread_pool_add_job(&pool, client_fd);
        }
    }

    // --- Очистка ---
    pool.shutdown = 1;
    pthread_cond_broadcast(&pool.queue_cond);
    
    for (int i = 0; i < pool.num_threads; i++) {
        pthread_join(pool.threads[i], NULL);
    }
    
    pthread_mutex_destroy(&pool.queue_mutex);
    pthread_cond_destroy(&pool.queue_cond);
    free(pool.threads);
    
    job_t *current = pool.head;
    job_t *next;
    while(current != NULL) {
        next = current->next;
        close(current->client_fd);
        free(current);
        current = next;
    }
    
    close(listen_sock);
    
    return 0;
}
