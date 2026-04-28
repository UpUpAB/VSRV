/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: [Дата сдачи]
ПРАКТИЧЕСКАЯ РАБОТА: [Номер работы]
ИМЯ ФАЙЛА: http_client_select.c
ЗАДАНИЕ: Реализация HTTP-клиента с постраничным выводом ответа сервера
и интерактивным управлением прокруткой с использованием select().
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Программа представляет собой HTTP-клиент с расширенным функционалом
интерактивной прокрутки. Клиент подключается к указанному веб-серверу,
отправляет HTTP GET-запрос и выводит ответ с возможностью постраничной
прокрутки. Для обработки одновременных событий (ввод пользователя и
данные от сервера) используется системный вызов select(), позволяющий
реализовать однопоточный интерактивный интерфейс.
--------------------------------------------------
*/

// http_client_select.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>

// --- Константы ---
#define MAX_LINE_LEN 9000
#define HOSTNAME_SIZE 256
#define SCREEN_HEIGHT 25 // Максимальное количество строк на экране
#define BUFFER_SIZE 4096 // Размер буфера для чтения/записи данных

// Размер буфера для HTTP-запроса
#define HTTP_REQUEST_SIZE (MAX_LINE_LEN + 512)

/*
--------------------------------------------------
ОБЪЯВЛЕНИЕ ГЛОБАЛЬНЫХ ПЕРЕМЕННЫХ:
old_term - структура для сохранения оригинальных настроек терминала
--------------------------------------------------
*/
struct termios old_term; // Для сохранения оригинальных настроек терминала

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: set_cbreak_mode
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    tcgetattr() - получение текущих атрибутов терминала
    tcsetattr() - установка новых атрибутов терминала (режим cbreak)
    perror() - обработка ошибок системных вызовов
    exit() - аварийное завершение программы при критических ошибках
--------------------------------------------------
*/
void set_cbreak_mode() {
    struct termios new_term;

    if (tcgetattr(STDIN_FILENO, &old_term) == -1) {
        perror("tcgetattr failed");
        exit(EXIT_FAILURE);
    }
    
    new_term = old_term;

    // Отключить канонический режим (ICANON) и локальное эхо (ECHO)
    new_term.c_lflag &= ~(ICANON | ECHO);
    new_term.c_cc[VMIN] = 1; // Минимальное количество символов для чтения
    new_term.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term) == -1) {
        perror("tcsetattr failed");
        exit(EXIT_FAILURE);
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: reset_terminal_mode
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    tcsetattr() - восстановление исходных настроек терминала
    perror() - обработка ошибок при восстановлении настроек
--------------------------------------------------
*/
void reset_terminal_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term) == -1) {
        perror("tcsetattr reset failed");
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: parse_url
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    strdup() - создание копии строки для безопасной модификации
    strstr() - поиск подстроки (протокола) в URL
    strchr() - поиск символа '/' для разделения хоста и пути
    strncpy() - безопасное копирование подстрок
    fprintf() - вывод сообщений об ошибках
    free() - освобождение памяти, выделенной для копии URL
--------------------------------------------------
*/
int parse_url(const char *url, char *hostname, char *path) {
    char *url_copy = strdup(url);
    char *protocol_end = strstr(url_copy, "://");

    char *host_start;
    if (protocol_end) {
        host_start = protocol_end + 3;
    } else {
        host_start = url_copy;
    }

    char *path_start = strchr(host_start, '/');

    if (path_start) {
        // Копирование hostname
        size_t host_len = path_start - host_start;
        if (host_len >= HOSTNAME_SIZE) {
            fprintf(stderr, "Hostname too long!\n");
            free(url_copy);
            return 1;
        }

        strncpy(hostname, host_start, host_len);
        hostname[host_len] = '\0';
        
        // Копирование пути
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

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    parse_url() - извлечение хоста и пути из URL
    fprintf() - вывод сообщений об ошибках и подсказок
    memset() - обнуление структуры hints
    getaddrinfo() - разрешение доменного имени в IP-адрес
    socket() - создание TCP-сокета
    connect() - установка соединения с сервером
    freeaddrinfo() - освобождение информации об адресе
    snprintf() - форматирование HTTP-запроса
    send() - отправка HTTP-запроса на сервер
    set_cbreak_mode() - включение режима cbreak для терминала
    select() - мультиплексирование ввода с клавиатуры и данных от сокета
    recv() - прием данных от сервера
    read() - чтение ввода пользователя с клавиатуры
    fputc() - посимвольный вывод данных
    fflush() - принудительная очистка буферов вывода
    reset_terminal_mode() - восстановление исходных настроек терминала
    close() - закрытие сокета
    printf() - вывод информационных сообщений и разделителей
--------------------------------------------------
*/
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
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

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
        
        break;
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
    // max_fd используется для select, должно быть max(sock_fd, STDIN_FILENO)
    int max_fd = (sock_fd > STDIN_FILENO) ? sock_fd : STDIN_FILENO;

    printf("\n--- Ответ ---\n");

    // Главный цикл приема/вывода данных
    while (socket_active || is_scrolling_paused) { // Продолжаем цикл, пока сокет активен ИЛИ ожидаем ввод
        FD_ZERO(&read_fds);

        if (socket_active) {
            FD_SET(sock_fd, &read_fds);
        }

        if (is_scrolling_paused) {
            FD_SET(STDIN_FILENO, &read_fds);
        }
        
        if (!socket_active && !is_scrolling_paused) {
            break;
        }

        // Устанавливаем тайм-аут, чтобы select не блокировался вечно, если сокет активен, но пуст
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        // select блокируется, пока не произойдет событие на одном из FD
        int select_result = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (select_result == -1) {
            if (errno == EINTR) continue; // Обработка прерывания
            perror("select error");
            break;
        }

        if (select_result == 0) {
            if (socket_active) continue;
            break;
        }

        // --- Обработка ввода пользователя (STDIN) ---
        if (is_scrolling_paused && FD_ISSET(STDIN_FILENO, &read_fds)) {
            char key;
            // Считываем один символ
            if (read(STDIN_FILENO, &key, 1) == 1) {
                if (key == ' ') { // Пробел
                    // Возобновление прокрутки
                    is_scrolling_paused = 0;
                    line_count = 0; // Сбрасываем счетчик
                    printf("\r%79c\r", ' '); // Очищаем приглашение
                    fflush(stdout);
                } else if (key == 'q' || key == 'Q') {
                     // Выход из программы
                    socket_active = 0;
                    is_scrolling_paused = 0;
                    break;
                }
            }
        }

        // --- Обработка данных из сокета ---
        // Читаем, только если сокет активен И прокрутка не приостановлена (или нам нужно прочитать, чтобы очистить буфер)
        if (socket_active && FD_ISSET(sock_fd, &read_fds)) {
            bytes_received = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                
                // Вывод данных и постраничная прокрутка
                for (int i = 0; i < bytes_received; i++) {
                    if (!is_scrolling_paused) { // Выводим только если не в режиме паузы
                        fputc(buffer[i], stdout);
                        if (buffer[i] == '\n') {
                            line_count++;
                            if (line_count >= SCREEN_HEIGHT) {
                                is_scrolling_paused = 1;
                                // Выводим приглашение
                                printf("Press space to scroll down");
                                fflush(stdout);
                                break;
                            }
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
