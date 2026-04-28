
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
#include <termios.h>

// --- Константы ---
#define MAX_LINE_LEN 9000
#define HOSTNAME_SIZE 256
#define SCREEN_HEIGHT 25
#define BUFFER_SIZE 4096

// Размер буфера для HTTP-запроса: MAX_LINE_LEN (путь) + 512 байт (для hostname и заголовков)
#define HTTP_REQUEST_SIZE (MAX_LINE_LEN + 512)

// --- Глобальные переменные ---
struct termios old_term; // Для сохранения оригинальных настроек терминала

// --- Вспомогательные функции для работы с терминалом (cbreak mode) ---

/**
 * @brief Устанавливает режим cbreak: отключает канонический режим (ICANON) и эхо (ECHO).
 */
void set_cbreak_mode() {
    struct termios new_term;

    if (tcgetattr(STDIN_FILENO, &old_term) == -1) {
        perror("tcgetattr failed");
        exit(EXIT_FAILURE);
    }
    
    new_term = old_term;

    // Устанавливаем режим cbreak: отключить канонический режим (ICANON)
    // и отключить локальное эхо (ECHO).
    new_term.c_lflag &= ~(ICANON | ECHO);
    new_term.c_cc[VMIN] = 1; // Минимальное количество символов для чтения
    new_term.c_cc[VTIME] = 0; // Тайм-аут (не используется)

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term) == -1) {
        perror("tcsetattr failed");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Восстанавливает исходный режим терминала.
 */
void reset_terminal_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term) == -1) {
        perror("tcsetattr reset failed");
    }
}


// --- Вспомогательные функции для парсинга URI ---

/**
 * @brief Извлекает имя хоста и путь из URI.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int parse_url(const char *url, char *hostname, char *path) {
    char *url_copy = strdup(url); // Копия для модификации
    char *protocol_end = strstr(url_copy, "://");

    char *host_start;
    if (protocol_end) {
        // Протокол найден, начинаем после "://"
        host_start = protocol_end + 3;
    } else {
        // Протокол отсутствует, начинаем с начала
        host_start = url_copy;
    }

    char *path_start = strchr(host_start, '/');

    if (path_start) {
        // Хостнейм занимает все до первого '/'
        size_t host_len = path_start - host_start;
        if (host_len >= HOSTNAME_SIZE) {
            fprintf(stderr, "Hostname too long!\n");
            free(url_copy);
            return -1;
        }

        // Копируем hostname
        strncpy(hostname, host_start, host_len);
        hostname[host_len] = '\0';
        
        // Копируем путь
        strncpy(path, path_start, MAX_LINE_LEN - 1);
        path[MAX_LINE_LEN - 1] = '\0';
    } else {
        // Путь отсутствует, только хост
        strncpy(hostname, host_start, HOSTNAME_SIZE - 1);
        hostname[HOSTNAME_SIZE - 1] = '\0';
        strcpy(path, "/"); // Путь по умолчанию
    }

    free(url_copy);
    return 0;
}

// --- Основная функция HTTP-клиента ---

int main(int argc, char *argv[]) {
    char hostname[HOSTNAME_SIZE];
    char path[MAX_LINE_LEN];

    if (argc != 2) {
        fprintf(stderr, "Использование: %s <URL>\n", argv[0]);
        return 1;
    }

    // Парсинг URL
    if (parse_url(argv[1], hostname, path) != 0) {
        return 1;
    }
    
    const char *port = "80"; // HTTP по умолчанию
    int sock_fd;
    struct addrinfo hints, *servinfo, *p;
    
    // --- 1. Подготовка сокета и запрос DNS ---
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // IPv4 или IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    if (getaddrinfo(hostname, port, &hints, &servinfo) != 0) {
        fprintf(stderr, "Ошибка: Не удалось разрешить адрес %s\n", hostname);
        return 2;
    }

    // --- 2. Установка соединения ---
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("Ошибка сокета");
            continue;
        }

        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("Ошибка соединения");
            close(sock_fd);
            continue;
        }
        
        break; // Соединение установлено
    }

    if (p == NULL) {
        fprintf(stderr, "Ошибка: Не удалось подключиться к %s\n", hostname);
        freeaddrinfo(servinfo);
        return 2;
    }

    freeaddrinfo(servinfo);

    // --- 3. Отправка HTTP-запроса ---
    char request[HTTP_REQUEST_SIZE];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
             path, hostname);

    if (send(sock_fd, request, strlen(request), 0) == -1) {
        perror("Ошибка отправки запроса");
        close(sock_fd);
        return 3;
    }

    // --- 4. Прием и вывод данных с прокруткой через select() ---
    set_cbreak_mode();
    
    char buffer[BUFFER_SIZE];
    int bytes_received;
    int line_count = 0;
    int is_scrolling_paused = 0; // 0 - прокрутка активна, 1 - приостановлена
    int socket_active = 1;      // 1 - сокет открыт, 0 - закрыт/ошибка

    fd_set read_fds;
    int max_fd = sock_fd > STDIN_FILENO ? sock_fd : STDIN_FILENO;

    printf("\n--- Ответ ---\n");

    // Главный цикл приема/вывода данных
    while (socket_active || is_scrolling_paused) {
        FD_ZERO(&read_fds);

        // Всегда проверяем сокет, если он активен
        if (socket_active) {
            FD_SET(sock_fd, &read_fds);
        }

        // Проверяем STDIN только, если вывод приостановлен
        if (is_scrolling_paused) {
            FD_SET(STDIN_FILENO, &read_fds);
        }
        
        // Условие выхода из цикла
        if (!socket_active && !is_scrolling_paused) {
            break;
        }

        // Устанавливаем тайм-аут, чтобы избежать вечной блокировки
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int select_result = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (select_result == -1) {
            if (errno == EINTR) continue;
            perror("select error");
            break;
        }

        if (select_result == 0) {
            // Тайм-аут. Просто продолжаем, чтобы перепроверить условия
            if (socket_active) continue;
            break;
        }

        // --- Обработка ввода пользователя (STDIN) ---
        if (is_scrolling_paused && FD_ISSET(STDIN_FILENO, &read_fds)) {
            char key;
            // Читаем один символ (неблокирующая операция, т.к. select показал готовность)
            if (read(STDIN_FILENO, &key, 1) == 1 && key == ' ') {
                // Пользователь нажал пробел, возобновляем вывод
                is_scrolling_paused = 0;
                line_count = 0; // Сбрасываем счетчик для следующего экрана
                printf("\r%79c\r", ' '); // Очищаем приглашение
                fflush(stdout);
            }
        }

        // --- Обработка данных из сокета ---
        if (socket_active && !is_scrolling_paused && FD_ISSET(sock_fd, &read_fds)) {
            bytes_received = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                
                // Вывод данных и постраничная прокрутка
                for (int i = 0; i < bytes_received; i++) {
                    fputc(buffer[i], stdout);
                    if (buffer[i] == '\n') {
                        line_count++;
                        if (line_count >= SCREEN_HEIGHT) {
                            is_scrolling_paused = 1;
                            // Выводим приглашение
                            printf("Press space to scroll down; [Q] to quit...");
                            fflush(stdout);
                            // Прерываем вывод остатка буфера, переходим в режим ожидания
                            break;
                        }
                    }
                }
                fflush(stdout);
            } else if (bytes_received == 0) {
                // Соединение закрыто
                socket_active = 0;
            } else {
                // Ошибка чтения
                perror("recv");
                socket_active = 0;
            }
        }
    }

    // --- Очистка ---
    reset_terminal_mode();
    close(sock_fd);

    if (socket_active == 0) {
        printf("\n\n--- Конец ответа ---\n");
    }

    return 0;
}
