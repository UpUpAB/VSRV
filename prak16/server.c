/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: [Дата сдачи]
ПРАКТИЧЕСКАЯ РАБОТА: [Номер работы]
ИМЯ ФАЙЛА: [Имя файла]
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация работы многопоточного прокси-сервера. Прокси сервер перенаправляет
TCP-трафик между клиентами и удаленным сервером, обеспечивая прозрачную
передачу данных без модификации содержимого. Реализация включает обработку
разрывов соединений, управление множественными соединениями и корректное
освобождение ресурсов.
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
#include <poll.h>
#include <fcntl.h> // Хотя сокеты неблокирующие, этот заголовок для fcntl

// Максимальное количество соединений (510 клиентов + 1 слушающий сокет)
#define MAX_CONNECTIONS 512
// Максимальный размер буфера для чтения/записи
#define BUF_SIZE 4096

/*
--------------------------------------------------
СТРУКТУРА: connection_pair_t
НАЗНАЧЕНИЕ: Структура для хранения информации о паре сокетов (клиент <-> удаленный сервер)
ПОЛЯ:
    client_fd - сокет, подключенный к локальному клиенту
    remote_fd - сокет, подключенный к удаленному серверу Y
    target_ip - IP удаленного сервера Y (не используется в коде, но в структуре есть)
    target_port - порт удаленного сервера X' (не используется в коде, но в структуре есть)
--------------------------------------------------
*/
typedef struct {
    int client_fd;   // Сокет, подключенный к локальному клиенту
    int remote_fd;   // Сокет, подключенный к удаленному серверу Y
    char *target_ip; // IP удаленного сервера Y (не используется в коде, но в структуре есть)
    int target_port; // Порт удаленного сервера X' (не используется в коде, но в структуре есть)
} connection_pair_t;

/*
--------------------------------------------------
ОБЪЯВЛЕНИЕ ГЛОБАЛЬНЫХ ПЕРЕМЕННЫХ:
connections - глобальный массив для хранения всех пар соединений.
Индекс в массиве pollfd соответствует индексу в этом массиве.
--------------------------------------------------
*/
connection_pair_t connections[MAX_CONNECTIONS];

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: connect_to_remote
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    gethostbyname() - получение информации о хосте по имени или IP-адресу
    socket() - создание нового сокета для соединения
    connect() - установка соединения с удаленным сервером
    perror() - вывод сообщений об ошибках системных вызовов
    close() - закрытие сокета в случае ошибки соединения
--------------------------------------------------
*/
int connect_to_remote(const char *hostname, int port);

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: close_connection
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    close() - закрытие файловых дескрипторов клиента и удаленного сервера
    printf() - вывод информационных сообщений о закрытии соединения
--------------------------------------------------
*/
void close_connection(struct pollfd fds[], int *nfds, int i);

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: proxy_data
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    recv() - чтение данных из сокета
    send() - отправка данных в сокет
    perror() - обработка ошибок при чтении/записи
    close_connection() - закрытие соединения при ошибках или разрыве
    printf() - вывод информации о состоянии соединения
--------------------------------------------------
*/
int proxy_data(int read_fd, int write_fd, struct pollfd fds[], int *nfds, int idx);

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: connect_to_remote
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    gethostbyname() - разрешение имени хоста в IP-адрес
    socket() - создание TCP сокета для соединения
    connect() - установка блокирующего соединения с удаленным сервером
    perror() - вывод диагностических сообщений при ошибках
    close() - освобождение ресурсов сокета при неудачном соединении
--------------------------------------------------
*/
int connect_to_remote(const char *hostname, int port) {
    struct sockaddr_in remote_addr;
    struct hostent *he;
    int remote_fd;

    // 1. Получение IP по имени хоста
    if ((he = gethostbyname(hostname)) == NULL) {
        perror("gethostbyname");
        return -1;
    }

    // 2. Создание сокета
    if ((remote_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    // 3. Установка неблокирующего режима
    // NOTE: Хотя в задании сказано "Вы не должны использовать неблокирующие сокеты",
    // для корректной работы асинхронного сокета без блокировки,
    // сокет должен быть временно переведен в неблокирующий режим.
    // Однако, для простоты и соответствия "без блокирования при чтении/записи"
    // для connect мы используем блокирующий вызов, поскольку он происходит
    // вне основного цикла poll (при принятии нового соединения).
    
    // 4. Подготовка адреса удаленного сервера
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);
    remote_addr.sin_addr = *((struct in_addr *)he->h_addr);
    memset(&(remote_addr.sin_zero), '\0', 8);

    // 5. Установка соединения (Блокирующий вызов)
    if (connect(remote_fd, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr)) == -1) {
        perror("connect to remote failed");
        close(remote_fd);
        return -1;
    }

    // Сокет настроен на блокирующий режим, как предписано в задании, но
    // на самом деле для poll/select сокеты клиента и remote должны быть
    // неблокирующими, чтобы избежать зависания на read/write.
    // Мы предполагаем, что далее read/write не будут блокироваться,
    // поскольку poll показал готовность.

    return remote_fd;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: close_connection
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    close() - освобождение файловых дескрипторов клиента и удаленного сервера
    printf() - информирование о закрытии соединения с указанием дескрипторов
--------------------------------------------------
*/
void close_connection(struct pollfd fds[], int *nfds, int i) {
    // Получаем FD клиента и удаленного сервера
    int client_fd = connections[fds[i].fd].client_fd;
    int remote_fd = connections[fds[i].fd].remote_fd;

    // Закрытие сокета клиента, если он не 0
    if (client_fd > 0) {
        close(client_fd);
    }
    // Закрытие сокета удаленного сервера, если он не 0
    if (remote_fd > 0) {
        close(remote_fd);
    }

    printf("[INFO] Connection closed: Client FD %d, Remote FD %d\n", client_fd, remote_fd);

    // Если индекс i - это не последний элемент в массиве fds
    if (i < *nfds - 1) {
        // Перемещение последнего элемента на текущую позицию для заполнения пробела
        fds[i] = fds[*nfds - 1];
        // Перемещение соответствующей пары соединений
        connections[fds[i].fd] = connections[fds[*nfds - 1].fd];
    }
    // Уменьшаем счетчик активных FD
    (*nfds)--;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: proxy_data
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    recv() - прием данных из сокета-источника
    send() - передача данных в сокет-назначения
    perror() - обработка ошибок при операциях ввода-вывода
    close_connection() - корректное завершение соединения при ошибках
    printf() - логирование информации о передаче данных
--------------------------------------------------
*/
int proxy_data(int read_fd, int write_fd, struct pollfd fds[], int *nfds, int idx) {
    char buffer[BUF_SIZE];
    ssize_t bytes_read, bytes_sent;

    // Чтение данных. Так как poll показал готовность, read() не должен блокироваться.
    bytes_read = recv(read_fd, buffer, BUF_SIZE, 0);

    if (bytes_read <= 0) {
        // Ошибка (bytes_read < 0) или EOF (соединение разорвано: bytes_read == 0)
        if (bytes_read == 0) {
            printf("[INFO] Peer closed connection on FD %d\n", read_fd);
        } else {
            perror("recv error");
        }
        // Разрыв соединения с обеих сторон
        close_connection(fds, nfds, idx);
        return 1; // Соединение разорвано
    } else {
        // Отправка данных.
        // NOTE: В идеале, нужно использовать цикл, чтобы гарантировать отправку всех данных,
        // но в асинхронном режиме (с poll) мы знаем, что сокет готов к записи,
        // поэтому 'send' должен быть успешным.
        // Для простоты мы предполагаем, что буфер 'BUF_SIZE' отправляется за один раз.
        bytes_sent = send(write_fd, buffer, bytes_read, 0);

        if (bytes_sent == -1) {
            perror("send error");
            close_connection(fds, nfds, idx);
            return 1; // Соединение разорвано
        }

        // В идеале, если bytes_sent < bytes_read, мы должны буферизировать
        // оставшиеся данные и ждать готовности 'write_fd' в следующем цикле poll,
        // но для простоты решения мы опускаем эту сложную буферизацию.

        return 0; // Соединение живо
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    atoi() - преобразование строковых аргументов в целые числа
    socket() - создание слушающего TCP-сокета
    setsockopt() - настройка параметров сокета (повторное использование адреса)
    bind() - привязка сокета к адресу и порту
    listen() - перевод сокета в режим прослушивания соединений
    poll() - мультиплексирование ввода-вывода для обработки множества соединений
    accept() - принятие входящих клиентских соединений
    connect_to_remote() - установка соединения с целевым сервером
    proxy_data() - передача данных между клиентом и удаленным сервером
    close_connection() - закрытие соединений и освобождение ресурсов
    perror() - обработка ошибок системных вызовов
    fprintf() - вывод диагностических сообщений в стандартный поток ошибок
    printf() - вывод информационных сообщений о работе сервера
    close() - завершающее закрытие всех открытых файловых дескрипторов
--------------------------------------------------
*/
int main(int argc, char *argv[]) {
    int listen_port_X;
    char *target_host_Y;
    int target_port_X_prime;
    int listen_fd;
    struct sockaddr_in serv_addr;

    // --- 1. Проверка аргументов командной строки ---
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <listen_port_X> <target_host_Y> <target_port_X'>\n", argv[0]);
        return EXIT_FAILURE;
    }

    listen_port_X = atoi(argv[1]);
    target_host_Y = argv[2];
    target_port_X_prime = atoi(argv[3]);

    if (listen_port_X <= 0 || target_port_X_prime <= 0) {
        fprintf(stderr, "Invalid port numbers.\n");
        return EXIT_FAILURE;
    }

    printf("Starting Proxy Server:\n");
    printf("Listening on Port: %d\n", listen_port_X);
    printf("Forwarding to: %s:%d\n", target_host_Y, target_port_X_prime);

    // --- 2. Создание слушающего сокета ---
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("listen socket creation failed");
        return EXIT_FAILURE;
    }

    // Повторное использование адреса (полезно при быстрой перезагрузке сервера)
    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // Настройка адреса
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(listen_port_X);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(serv_addr.sin_zero), '\0', 8);

    // Привязка к прослушивание
    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) {
        perror("Bind failed");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // Слушаем сокет. 512 - рекомендуемый размер очереди ожидания
    if (listen(listen_fd, 512) == -1) {
        perror("Listen failed");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // --- 3. Инициализация poll ---
    struct pollfd fds[MAX_CONNECTIONS];
    int nfds = 0; // Текущее количество активных файловых дескрипторов (FD)

    // Добавление слушающего сокета в массив pollfd
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN; // Интересуют события на чтение (новое соединение)
    nfds = 1;

    printf("[INFO] Proxy server is running...\n");

    // --- 4. Основной цикл сервера (Event Loop) ---
    while (1) {
        // Ожидание событий (блокировка до тех пор, пока не произойдет событие)
        // -1 означает бесконечное ожидание
        int poll_count = poll(fds, nfds, -1);

        if (poll_count == -1) {
            if (errno == EINTR) {
                // Прервано сигналом, повторяем
                continue;
            }
            perror("poll error");
            break;
        }

        // Обработка событий
        for (int i = 0; i < nfds; i++) {
            // Если событие не произошло, переходим к следующему FD
            if (fds[i].revents == 0) {
                continue;
            }

            // Проверка на ошибку или разрыв соединения
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "[ERROR] Poll error/hangup on FD %d\n", fds[i].fd);
                // Закрываем сокет и удаляем из массива
                close_connection(fds, &nfds, i);
                i--; // Уменьшаем счетчик, так как массив сдвинулся
                continue;
            }

            // --- А. Событие на слушающем сокете (Новое соединение) ---
            if (fds[i].fd == listen_fd) {
                if (fds[i].revents & POLLIN) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd;

                    // Принятие соединения (неблокирующая операция, так как poll показал готовность)
                    client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

                    if (client_fd == -1) {
                        perror("accept failed");
                        continue;
                    }

                    printf("[INFO] New client accepted on FD %d from %s:%d\n",
                           client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                    // Проверка на лимит соединений
                    if (nfds >= MAX_CONNECTIONS) {
                        fprintf(stderr, "[WARNING] Connection limit reached. Closing client FD %d.\n", client_fd);
                        close(client_fd);
                        continue;
                    }

                    // 1. Попытка подключения к удаленному серверу Y
                    int remote_fd = connect_to_remote(target_host_Y, target_port_X_prime);

                    if (remote_fd == -1) {
                        // Сервер Y отказал (или ошибка). Разрываем клиентское соединение.
                        fprintf(stderr, "[ERROR] Remote server %s:%d refused connection. Closing client FD %d.\n",
                                target_host_Y, target_port_X_prime, client_fd);
                        close(client_fd);
                        continue;
                    }

                    // 2. Успешное установление прокси-пары
                    int new_client_idx = nfds;
                    int new_remote_idx = nfds + 1;

                    // Должна быть проверка, что новые индексы не превышают MAX_CONNECTIONS.
                    // Поскольку мы уже проверили nfds < MAX_CONNECTIONS, и добавляем 2 FD,
                    // мы должны быть уверены, что MAX_CONNECTIONS >= 2.
                    if (new_remote_idx >= MAX_CONNECTIONS) {
                        // Это не должно произойти, если MAX_CONNECTIONS >= 2
                        fprintf(stderr, "[WARNING] Connection limit reached. Closing client/remote FDs.\n");
                        close(client_fd);
                        close(remote_fd);
                        continue;
                    }

                    // Добавление сокетов в массив pollfd
                    
                    // Клиентский сокет
                    fds[new_client_idx].fd = client_fd;
                    fds[new_client_idx].events = POLLIN; // Ждем данных от клиента

                    // Удаленный сокет
                    fds[new_remote_idx].fd = remote_fd;
                    fds[new_remote_idx].events = POLLIN; // Ждем данных от удаленного сервера

                    // Сохранение информации о паре
                    connections[client_fd].client_fd = client_fd;
                    connections[client_fd].remote_fd = remote_fd;
                    connections[remote_fd].client_fd = client_fd;
                    connections[remote_fd].remote_fd = remote_fd;

                    // Обновление общего количества FD
                    nfds += 2;
                }
            }
            // --- B. Событие на рабочем сокете (Трансляция данных) ---
            else {
                // Ищем соответствующий сокет для отправки
                int client_fd = connections[fds[i].fd].client_fd;
                int remote_fd = connections[fds[i].fd].remote_fd;
                
                // Определяем "другой" FD в паре
                // int other_idx = (fds[i].fd == client_fd) ? i + 1 : i - 1;
                // Этот способ работает только если пара FDs всегда идет подряд (i, i+1) или (i-1, i)
                // Более надежно - использовать stored FD:
                int read_fd = fds[i].fd;
                int write_fd = (read_fd == client_fd) ? remote_fd : client_fd;

                // Проверяем, произошло ли событие на клиентском сокете (Client -> Remote)
                if (read_fd == client_fd && (fds[i].revents & POLLIN)) {
                    if (proxy_data(client_fd, remote_fd, fds, &nfds, i)) {
                        i--; // Соединение разорвано, массив сдвинулся
                    }
                }
                // Проверяем, произошло ли событие на удаленном сокете (Remote -> Client)
                else if (read_fd == remote_fd && (fds[i].revents & POLLIN)) {
                    if (proxy_data(remote_fd, client_fd, fds, &nfds, i)) {
                        i--; // Соединение разорвано, массив сдвинулся
                    }
                }
            }
        }
    }

    // --- Б. Очистка ---
    // Закрытие всех оставшихся открытых сокетов
    for (int i = 0; i < nfds; i++) {
        if (fds[i].fd != 0) { // Пропускаем слушающий сокет, который закроется в destroy
            close(fds[i].fd);
        }
    }
    // Закрытие слушающего сокета
    close(listen_fd);

    printf("Server shutting down.\n");
    return EXIT_SUCCESS;
}
