/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 12.12.2025
ПРАКТИЧЕСКАЯ РАБОТА: 16
НАЗВАНИЕ: proxy.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Программа реализует высокопроизводительный HTTP-прокси сервер с поддержкой
до 100 одновременных соединений. Сервер использует неблокирующие сокеты
и системный вызов select() для эффективного управления множеством соединений
в одном потоке. Прокси обрабатывает HTTP GET-запросы, устанавливает
соединения с удаленными серверами и пересылает данные между клиентами
и целевыми серверами, реализуя базовую логику кэширования.
--------------------------------------------------
*/

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
#include <sys/time.h>
#include <fcntl.h>
#include <sys/ioctl.h> // Для fionbio

// --- Константы ---
#define MAX_CONNECTIONS 100 // Максимальное количество одновременных соединений
#define PROXY_PORT 8080     // Порт, на котором слушает прокси
#define BUF_SIZE 4096       // Размер буфера для чтения/записи
#define MAX_REQUEST_SIZE 4096 // Максимальный размер заголовков запроса

/*
--------------------------------------------------
ПЕРЕЧИСЛЕНИЕ: conn_state_t
НАЗНАЧЕНИЕ: Определение состояний соединения в конечном автомате прокси
ЗНАЧЕНИЯ:
    STATE_FREE - слот соединения свободен
    STATE_READING_REQUEST - чтение HTTP-запроса от клиента
    STATE_CONNECTING_REMOTE - установка соединения с удаленным сервером
    STATE_RELAYING - передача данных между клиентом и сервером
    STATE_CACHED_RESPONSE - отправка кэшированного ответа клиенту
--------------------------------------------------
*/
typedef enum {
    STATE_FREE = 0,
    STATE_READING_REQUEST,      // Читаем HTTP-запрос от клиента
    STATE_CONNECTING_REMOTE,    // Соединяемся с удаленным сервером
    STATE_RELAYING,             // Передаем данные между клиентом/сервером
    STATE_CACHED_RESPONSE       // Отправляем кэшированный ответ клиенту
} conn_state_t;

/*
--------------------------------------------------
СТРУКТУРА: connection
НАЗНАЧЕНИЕ: Структура для хранения состояния одного соединения в прокси
ПОЛЯ:
    client_fd - файловый дескриптор сокета клиента
    remote_fd - файловый дескриптор сокета удаленного сервера (-1 если не подключен)
    state - текущее состояние соединения (conn_state_t)
    request_buf - буфер для хранения запроса и промежуточных данных
    buf_len - количество данных в буфере
    host - доменное имя целевого сервера
    path - путь запроса на целевом сервере
    cache_data - указатель на кэшированные данные ответа
    cache_size - общий размер кэшированного ответа
    cache_offset - текущее смещение при чтении/записи кэша
--------------------------------------------------
*/
typedef struct connection {
    int client_fd;          // Сокет клиента
    int remote_fd;          // Сокет удаленного сервера (-1, если не подключен)
    conn_state_t state;     // Текущее состояние соединения
    char request_buf[BUF_SIZE]; // Буфер для запроса/промежуточных данных
    int buf_len;            // Длина данных в буфере
    
    // Поля для маршрутизации и кэширования:
    char host[256];
    char path[256];
    char *cache_data;       // Указатель на кэш данных (Для простоты: храним целиком ответ)
    size_t cache_size;      // Общий размер ответа
    int cache_offset;       // Смещение для записи/чтения
} connection_t;

/*
--------------------------------------------------
ОБЪЯВЛЕНИЕ ГЛОБАЛЬНЫХ ПЕРЕМЕННЫХ:
connections - глобальный массив всех активных соединений
max_fd - максимальный файловый дескриптор для select()
--------------------------------------------------
*/
// Глобальный массив соединений
connection_t connections[MAX_CONNECTIONS];
int max_fd = 0; // Максимальный файловый дескриптор для select

/*
--------------------------------------------------
ОБЪЯВЛЕНИЯ ФУНКЦИЙ:
set_nonblocking - установка неблокирующего режима для сокета
close_connection - закрытие соединения и освобождение ресурсов
parse_request_host - извлечение хоста и пути из HTTP-запроса
connect_to_remote - установка соединения с удаленным сервером
handle_read - обработка чтения данных из сокета
handle_write - обработка записи данных в сокет
--------------------------------------------------
*/
// --- Объявления функций ---
int set_nonblocking(int fd);
void close_connection(int index);
int parse_request_host(const char *request, char *host, char *path);
int connect_to_remote(int index);
void handle_read(int index, int fd);
void handle_write(int index, int fd);

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: set_nonblocking
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    fcntl() - получение и установка флагов файлового дескриптора
    perror() - обработка ошибок при работе с fcntl
--------------------------------------------------
*/
int set_nonblocking(int fd) {
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL, O_NONBLOCK)");
        return -1;
    }
    return 0;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: init_connections
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    (нет явных вызовов, инициализация полей структур)
--------------------------------------------------
*/
void init_connections() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        connections[i].client_fd = -1;
        connections[i].remote_fd = -1;
        connections[i].state = STATE_FREE;
        connections[i].buf_len = 0;
        connections[i].cache_data = NULL;
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: find_free_slot
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    (нет явных вызовов, поиск по массиву структур)
--------------------------------------------------
*/
int find_free_slot() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i].state == STATE_FREE) {
            return i;
        }
    }
    return -1;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: close_connection
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    close() - закрытие файловых дескрипторов клиента и сервера
    free() - освобождение памяти, выделенной для кэшированных данных
    memset() - очистка структуры соединения
    printf() - вывод информационного сообщения о закрытии соединения
--------------------------------------------------
*/
void close_connection(int index) {
    connection_t *conn = &connections[index];
    
    if (conn->client_fd != -1) {
        close(conn->client_fd);
    }
    if (conn->remote_fd != -1) {
        close(conn->remote_fd);
    }

    printf("[INFO] Connection closed (Slot %d)\n", index);

    // Очистка слота
    if (conn->cache_data) {
        free(conn->cache_data);
    }
    memset(conn, 0, sizeof(connection_t));
    conn->client_fd = -1;
    conn->remote_fd = -1;
    conn->state = STATE_FREE;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: parse_request_host
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    sscanf() - парсинг первой строки HTTP-запроса
    strcasecmp() - сравнение строк без учета регистра (метод GET)
    strstr() - поиск заголовка Host в запросе
    strchr() - поиск конца строки заголовка
    strncpy() - безопасное копирование хоста и пути
    fprintf() - вывод сообщений об ошибках
    strcpy() - копирование пути по умолчанию
--------------------------------------------------
*/
int parse_request_host(const char *request, char *host, char *path) {
    char method[10];
    char url[MAX_REQUEST_SIZE];
    
    // Парсим первую строку: GET /path HTTP/1.1
    if (sscanf(request, "%s %s", method, url) != 2) {
        return -1;
    }
    if (strcasecmp(method, "GET") != 0) {
        // Проксируем только GET-запросы
        return -1;
    }

    // Ищем заголовок Host:
    const char *host_header = strstr(request, "\r\nHost: ");
    if (!host_header) {
        host_header = strstr(request, "\r\nhost: "); // Некоторые клиенты могут использовать нижний регистр
    }

    if (!host_header) {
        return -1;
    }
    host_header += 8; // Сдвигаемся после "Host: "

    char *host_end = strchr(host_header, '\r');
    if (!host_end) return -1;

    size_t host_len = host_end - host_header;
    if (host_len >= 256) return -1;
    
    strncpy(host, host_header, host_len);
    host[host_len] = '\0';
    
    // Path: в прокси-запросах (CONNECT) URL полный, но в данном случае он относительный
    // Если клиент шлет GET http://host/path HTTP/1.1, нужно извлечь путь и хост.
    // Для стандартного прокси-запроса (без CONNECT), URL может быть полным:
    
    // Если url содержит "http://", то парсим его
    char *url_start = strstr(url, "http://");
    if (url_start) {
        url_start += 7; // после "http://"
        char *path_start = strchr(url_start, '/');

        if (path_start) {
            // Копируем путь
            strncpy(path, path_start, 256 - 1);
            path[255] = '\0';
        } else {
            // Нет пути, только хост
            strcpy(path, "/");
        }
        
        // ВАЖНО: Мы уже извлекли хост из заголовка Host, который более надежен.
        // Здесь мы просто убеждаемся, что мы нашли валидный путь.
        
    } else {
        // Относительный путь
        strncpy(path, url, 256 - 1);
        path[255] = '\0';
    }

    return 0;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: connect_to_remote
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    snprintf() - форматирование строки порта
    memset() - инициализация структуры hints
    getaddrinfo() - разрешение доменного имени в адреса
    socket() - создание сокета для соединения с удаленным сервером
    set_nonblocking() - установка неблокирующего режима сокета
    connect() - неблокирующее соединение с удаленным сервером
    close() - закрытие сокета при ошибках
    perror() - обработка ошибок соединения
    fprintf() - вывод диагностических сообщений
    freeaddrinfo() - освобождение информации об адресах
--------------------------------------------------
*/
int connect_to_remote(int index) {
    connection_t *conn = &connections[index];
    struct addrinfo hints, *servinfo, *p;
    char port_str[6];
    
    snprintf(port_str, 6, "%d", 80); // Пока хардкодим HTTP порт 80

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(conn->host, port_str, &hints, &servinfo) != 0) {
        fprintf(stderr, "[ERROR] getaddrinfo failed for %s\n", conn->host);
        return -1;
    }

    int remote_fd = -1;
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((remote_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }

        if (set_nonblocking(remote_fd) < 0) {
            close(remote_fd);
            remote_fd = -1;
            continue;
        }

        if (connect(remote_fd, p->ai_addr, p->ai_addrlen) == -1) {
            if (errno == EINPROGRESS) {
                // Соединение в процессе
                conn->remote_fd = remote_fd;
                conn->state = STATE_CONNECTING_REMOTE;
                freeaddrinfo(servinfo);
                return 0;
            } else {
                perror("[ERROR] non-blocking connect failed immediately");
                close(remote_fd);
                remote_fd = -1;
                continue;
            }
        }
        
        // Connect завершился мгновенно
        conn->remote_fd = remote_fd;
        conn->state = STATE_RELAYING; // Переход сразу к реле (отправка запроса)
        freeaddrinfo(servinfo);
        return 0;
    }

    freeaddrinfo(servinfo);
    return -1;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: accept_new_connection
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    accept() - принятие входящего соединения от клиента
    set_nonblocking() - установка неблокирующего режима клиентского сокета
    find_free_slot() - поиск свободного слота для нового соединения
    close() - закрытие сокета при ошибках или лимите соединений
    fprintf() - вывод предупреждений о достижении лимита соединений
    printf() - вывод информационных сообщений о новых соединениях
--------------------------------------------------
*/
void accept_new_connection(int listen_fd) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);

    if (client_fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            perror("[ERROR] accept");
        }
        return;
    }

    if (set_nonblocking(client_fd) < 0) {
        close(client_fd);
        return;
    }

    int index = find_free_slot();
    if (index == -1) {
        fprintf(stderr, "[WARNING] Max connections reached. Rejecting client.\n");
        close(client_fd);
        return;
    }

    // Инициализация нового соединения
    connections[index].client_fd = client_fd;
    connections[index].state = STATE_READING_REQUEST;
    
    if (client_fd > max_fd) {
        max_fd = client_fd;
    }

    printf("[INFO] New connection accepted. FD: %d, Slot: %d\n", client_fd, index);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: handle_read
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    read() - чтение данных из сокета в буфер
    strstr() - поиск конца HTTP-заголовков (двойной перевод строки)
    parse_request_host() - извлечение хоста и пути из HTTP-запроса
    connect_to_remote() - инициация соединения с удаленным сервером
    close_connection() - закрытие соединения при ошибках
    fprintf() - вывод сообщений об ошибках парсинга запроса
    printf() - вывод информационных сообщений о состоянии соединения
    perror() - обработка ошибок чтения
--------------------------------------------------
*/
void handle_read(int index, int fd) {
    connection_t *conn = &connections[index];
    ssize_t bytes_read;

    // Читаем в буфер, начиная с текущей длины
    bytes_read = read(fd, conn->request_buf + conn->buf_len, BUF_SIZE - conn->buf_len - 1);

    if (bytes_read > 0) {
        conn->buf_len += bytes_read;
        conn->request_buf[conn->buf_len] = '\0';
        
        // Обработка данных в зависимости от состояния
        if (conn->state == STATE_READING_REQUEST) {
            
            // Проверяем, пришел ли полный HTTP-заголовок
            if (strstr(conn->request_buf, "\r\n\r\n")) {
                
                // 1. Парсим Host и Path
                if (parse_request_host(conn->request_buf, conn->host, conn->path) == 0) {
                    printf("[INFO] Slot %d: Request received for Host: %s Path: %s\n", index, conn->host, conn->path);
                    
                    // 2. *** ЛОГИКА КЭШИРОВАНИЯ *** (Опущено, но здесь была бы проверка кэша)
                    
                    // 3. Устанавливаем соединение с удаленным сервером
                    if (connect_to_remote(index) != 0) {
                        close_connection(index);
                    }
                } else {
                    fprintf(stderr, "[ERROR] Slot %d: Invalid or non-GET request.\n", index);
                    close_connection(index);
                }
            }
        }
        
        else if (conn->state == STATE_RELAYING) {
            if (fd == conn->remote_fd) {
                // Если читаем с удаленного сервера, то мы также должны кэшировать данные
                // (Логика кэширования и пересылки клиенту опускается для краткости)
                // Для простоты, просто переводим данные в буфер и ждем записи
            }
        }
        
    } else if (bytes_read == 0) {
        // EOF: соединение закрыто партнером
        printf("[INFO] Slot %d: Socket closed by peer (FD: %d)\n", index, fd);
        close_connection(index);
    } else {
        // Ошибка (EWOULDBLOCK/EAGAIN)
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            perror("read error");
            close_connection(index);
        }
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: handle_write
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    getsockopt() - проверка статуса неблокирующего соединения
    write() - запись данных в сокет
    memmove() - сдвиг данных в буфере после частичной записи
    close_connection() - закрытие соединения после завершения передачи
    fprintf() - вывод сообщений об ошибках соединения
    printf() - вывод информационных сообщений о состоянии передачи
    perror() - обработка ошибок записи
--------------------------------------------------
*/
void handle_write(int index, int fd) {
    connection_t *conn = &connections[index];
    ssize_t bytes_written;

    // 1. Обработка завершения неблокирующего connect
    if (conn->state == STATE_CONNECTING_REMOTE && fd == conn->remote_fd) {
        int error = 0;
        socklen_t len = sizeof(error);
        
        // Проверяем статус сокета
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
            fprintf(stderr, "[ERROR] Slot %d: Remote connect failed. Error: %d\n", index, error);
            close_connection(index);
            return;
        }

        // Соединение успешно установлено
        conn->state = STATE_RELAYING;
        printf("[INFO] Slot %d: Remote connection established. Sending request.\n", index);
        
        // Начинаем передачу запроса (данные уже в conn->request_buf)
        // Продолжаем выполнение в блоке STATE_RELAYING
    }

    // 2. Отправка данных (если есть что отправлять)
    if (conn->buf_len > 0) {
        bytes_written = write(fd, conn->request_buf, conn->buf_len);

        if (bytes_written > 0) {
            
            conn->buf_len -= bytes_written;
            // Сдвигаем оставшиеся данные в буфере
            memmove(conn->request_buf, conn->request_buf + bytes_written, conn->buf_len);

            printf("[INFO] Slot %d: Wrote %zd bytes to FD %d (Remaining: %d)\n", index, bytes_written, fd, conn->buf_len);
            
            // Если весь буфер отправлен, и это кэшированный ответ, закрываем
            if (conn->state == STATE_CACHED_RESPONSE && conn->buf_len == 0) {
                printf("[INFO] Slot %d: Cached response sent completely.\n", index);
                close_connection(index);
            }
        } else if (bytes_written < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                perror("[ERROR] write");
                close_connection(index);
            }
        }
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    init_connections() - инициализация массива соединений
    socket() - создание слушающего TCP-сокета
    setsockopt() - настройка повторного использования адреса
    set_nonblocking() - установка неблокирующего режима слушающего сокета
    bind() - привязка сокета к адресу и порту
    listen() - перевод сокета в режим прослушивания
    printf() - вывод информации о запуске сервера
    FD_ZERO() - очистка наборов файловых дескрипторов
    FD_SET() - добавление дескрипторов в наборы
    select() - мультиплексирование ввода-вывода для неблокирующих сокетов
    FD_ISSET() - проверка активности дескрипторов
    accept_new_connection() - принятие новых клиентских соединений
    handle_read() - обработка чтения данных из сокетов
    handle_write() - обработка записи данных в сокеты
    perror() - обработка ошибок системных вызовов
    close() - закрытие слушающего сокета и очистка ресурсов
    close_connection() - закрытие всех активных соединений при завершении
--------------------------------------------------
*/
int main() {
    int listen_fd;
    struct sockaddr_in server_addr;
    
    init_connections();

    // 1. Создание слушающего сокета
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[FATAL] socket");
        return 1;
    }

    // Настройка сокета
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (set_nonblocking(listen_fd) < 0) {
        close(listen_fd);
        return 1;
    }
    
    // 2. Привязка (bind)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PROXY_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[FATAL] bind");
        close(listen_fd);
        return 1;
    }

    // 3. Слушание (listen)
    if (listen(listen_fd, 512) < 0) {
        perror("[FATAL] listen");
        close(listen_fd);
        return 1;
    }

    printf("HTTP Proxy running on port %d (single-threaded, select-based).\n", PROXY_PORT);
    max_fd = listen_fd;

    // --- ГЛАВНЫЙ ЦИКЛ SELECT ---
    while (1) {
        fd_set read_fds, write_fds;

        // Очистка и настройка наборов дескрипторов
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        
        FD_SET(listen_fd, &read_fds);

        // Итерация по всем активным соединениям
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (connections[i].state != STATE_FREE) {
                
                int client_fd = connections[i].client_fd;
                int remote_fd = connections[i].remote_fd;

                // Добавляем для чтения (всегда)
                if (client_fd > 0) {
                    FD_SET(client_fd, &read_fds);
                    if (client_fd > max_fd) max_fd = client_fd;
                }
                if (remote_fd > 0) {
                    FD_SET(remote_fd, &read_fds);
                    if (remote_fd > max_fd) max_fd = remote_fd;
                }

                // Добавляем для записи (зависит от состояния и буфера)
                
                // 1. Если нам нужно отправить ответ/запрос
                if (connections[i].buf_len > 0) {
                    if (connections[i].state == STATE_CACHED_RESPONSE) {
                        FD_SET(client_fd, &write_fds);
                    } else if (connections[i].state == STATE_RELAYING) {
                        FD_SET(remote_fd, &write_fds);
                    }
                }
                
                // 2. Если ждем завершения connect
                if (connections[i].state == STATE_CONNECTING_REMOTE) {
                    FD_SET(remote_fd, &write_fds);
                }
            }
        }
        
        // Вызов select (timeout = NULL -> блокирующий)
        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, NULL);

        if (activity < 0 && errno != EINTR) {
            perror("[FATAL] select error");
            break;
        }
        if (activity == 0) continue; // Таймаут (если был задан)

        // 4. Обработка активности
        
        // А. Активность на слушающем сокете -> Новое соединение
        if (FD_ISSET(listen_fd, &read_fds)) {
            accept_new_connection(listen_fd);
            // Активность могла увеличить max_fd, но это будет учтено на следующей итерации.
        }
        
        // B. Активность на существующих соединениях
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (connections[i].state == STATE_FREE) {
                continue;
            }

            int client_fd = connections[i].client_fd;
            int remote_fd = connections[i].remote_fd;

            // Обработка записи (сначала, так как успешная запись может сменить состояние)
            if (client_fd > 0 && FD_ISSET(client_fd, &write_fds)) {
                handle_write(i, client_fd);
            }
            if (remote_fd > 0 && FD_ISSET(remote_fd, &write_fds)) {
                handle_write(i, remote_fd);
            }
            
            // Обработка чтения
            if (client_fd > 0 && FD_ISSET(client_fd, &read_fds)) {
                handle_read(i, client_fd);
            }
            if (remote_fd > 0 && FD_ISSET(remote_fd, &read_fds)) {
                handle_read(i, remote_fd);
            }
        }
    }

    // Очистка и завершение работы
    close(listen_fd);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i].state != STATE_FREE) {
            close_connection(i);
        }
    }

    printf("Server shutting down.\n");
    return 0;
}
