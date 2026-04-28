

#include "msgqueue.h"

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: mymsginit
НАЗНАЧЕНИЕ: Инициализация очереди сообщений и всех связанных синхронизационных примитивов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    q - указатель на очередь сообщений для инициализации
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_init() - инициализация мьютекса для защиты критических секций
    sem_init() - инициализация семафоров для управления свободными и занятыми слотами
    pthread_cond_init() - инициализация условной переменной для операции drop
    perror() - обработка ошибок инициализации синхронизационных примитивов
    exit() - аварийное завершение программы при критических ошибках инициализации
--------------------------------------------------
*/
void mymsginit(queue_t *q) {
    // Инициализация структуры
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->dropped = false;

    // Инициализация мьютекса
    if (pthread_mutex_init(&q->mutex, NULL) != 0) {
        perror("pthread_mutex_init failed");
        exit(EXIT_FAILURE);
    }
    
    // Инициализация семафора full (количество занятых слотов) -> начальное значение 0
    if (sem_init(&q->full, 0, 0) != 0) {
        perror("sem_init full failed");
        exit(EXIT_FAILURE);
    }

    // Инициализация семафора empty (количество свободных слотов) -> начальное значение MAX_QUEUE_SIZE
    if (sem_init(&q->empty, 0, MAX_QUEUE_SIZE) != 0) {
        perror("sem_init empty failed");
        exit(EXIT_FAILURE);
    }
    
    // Инициализация условной переменной (для mymsqdrop)
    if (pthread_cond_init(&q->drop_cond, NULL) != 0) {
        perror("pthread_cond_init failed");
        exit(EXIT_FAILURE);
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: mymsgput
НАЗНАЧЕНИЕ: Добавление нового сообщения в очередь (потокобезопасная операция)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    q - указатель на очередь сообщений
    msg - строка с сообщением для добавления
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    Количество успешно записанных символов (0 при ошибке или выполнении drop)
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    sem_wait() - ожидание свободного слота в очереди (семафор empty)
    sem_post() - сигнализация о появлении нового сообщения (семафор full)
    pthread_mutex_lock() - захват мьютекса для защиты критической секции
    pthread_mutex_unlock() - освобождение мьютекса
    strnlen() - определение длины строки с ограничением
    strncpy() - копирование строки с ограничением длины
    perror() - обработка ошибок операций с семафорами
--------------------------------------------------
*/
int mymsgput(queue_t *q, char *msg) {
    int transferred_chars = 0;
    
    // 1. Ожидание свободного слота (потребление семафора empty)
    // Блокируется, если очередь полна (empty == 0)
    if (sem_wait(&q->empty) != 0) {
        perror("sem_wait empty failed");
        return 0; // Ошибка
    }

    // Захват мьютекса для критической секции
    pthread_mutex_lock(&q->mutex);
    
    // Проверка флага mymsqdrop (должна быть внутри критической секции после sem_wait,
    // чтобы mymsqdrop мог разблокировать ожидающие нити)
    if (q->dropped) {
        // Освобождение мьютекса
        pthread_mutex_unlock(&q->mutex);
        // Возвращаем семафор empty, который мы взяли
        sem_post(&q->empty);
        return 0;
    }
    
    // Обрезаем сообщение до MAX_MSG_LEN - 1 (80 символов)
    size_t len = strnlen(msg, MAX_MSG_LEN - 1);
    
    // Копирование данных в буфер
    strncpy(q->buffer[q->tail].data, msg, len);
    q->buffer[q->tail].data[len] = '\0'; // Гарантируем ASCIIZ
    transferred_chars = len;

    // Сдвиг хвоста
    q->tail = (q->tail + 1) % MAX_QUEUE_SIZE;
    q->count++;
    
    // Освобождение мьютекса
    pthread_mutex_unlock(&q->mutex);
    
    // 2. Сигнализация о занятом слоте (производство семафора full)
    sem_post(&q->full);
    
    return transferred_chars;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: mymsgget
НАЗНАЧЕНИЕ: Извлечение сообщения из очереди (потокобезопасная операция)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    q - указатель на очередь сообщений
    buf - буфер для сохранения извлеченного сообщения
    bufsize - размер буфера для сохранения сообщения
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    Количество успешно прочитанных символов (0 при ошибке или выполнении drop)
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    sem_wait() - ожидание сообщения в очереди (семафор full)
    sem_post() - сигнализация об освобождении слота (семафор empty)
    pthread_mutex_lock() - захват мьютекса для защиты критической секции
    pthread_mutex_unlock() - освобождение мьютекса
    strnlen() - определение длины строки в очереди
    strncpy() - копирование строки в пользовательский буфер
    perror() - обработка ошибок операций с семафорами
--------------------------------------------------
*/
int mymsgget(queue_t *q, char *buf, size_t bufsize) {
    int read_chars = 0;
    
    // 1. Ожидание занятого слота (потребление семафора full)
    // Блокируется, если очередь пуста (full == 0)
    if (sem_wait(&q->full) != 0) {
        perror("sem_wait full failed");
        return 0; // Ошибка
    }

    // Захват мьютекса для критической секции
    pthread_mutex_lock(&q->mutex);
    
    // Проверка флага mymsqdrop
    if (q->dropped) {
        // Освобождение мьютекса
        pthread_mutex_unlock(&q->mutex);
        // Возвращаем семафор full, который мы взяли
        sem_post(&q->full);
        return 0;
    }
    
    // Копирование данных в пользовательский буфер. Обрезаем до bufsize - 1.
    size_t len = strnlen(q->buffer[q->head].data, MAX_MSG_LEN - 1);
    size_t copy_len = len < (bufsize - 1) ? len : (bufsize - 1);
    
    strncpy(buf, q->buffer[q->head].data, copy_len);
    buf[copy_len] = '\0'; // Гарантируем ASCIIZ
    read_chars = copy_len;
    
    // Очистка (необязательно, но полезно) и сдвиг головы
    // memset(q->buffer[q->head].data, 0, MAX_MSG_LEN); // Оставим данные, так как они не влияют на работу
    q->head = (q->head + 1) % MAX_QUEUE_SIZE;
    q->count--;
    
    // Освобождение мьютекса
    pthread_mutex_unlock(&q->mutex);
    
    // 2. Сигнализация о свободном слоте (производство семафора empty)
    sem_post(&q->empty);
    
    return read_chars;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: mymsqdrop
НАЗНАЧЕНИЕ: Разблокировка всех ожидающих операций get и put с очисткой состояния очереди
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    q - указатель на очередь сообщений
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для защиты операции drop
    pthread_mutex_unlock() - освобождение мьютекса
    sem_getvalue() - получение текущего значения семафоров
    sem_post() - разблокировка потоков, ожидающих на семафорах
    printf() - уведомление о выполнении операции drop
--------------------------------------------------
*/
void mymsqdrop(queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    
    if (q->dropped) {
        pthread_mutex_unlock(&q->mutex);
        return; // Уже было выполнено
    }
    
    q->dropped = true;
    
    // Освобождение всех потенциально заблокированных нитей get и put.
    // Если нить заблокирована на sem_wait, она должна быть разблокирована.
    // Максимальное количество нитей, которые могли заблокироваться:
    // - На mymsgput: MAX_QUEUE_SIZE нитей, заблокированных на q->empty.
    // - На mymsgget: MAX_QUEUE_SIZE нитей, заблокированных на q->full.
    
    // Разблокировка заблокированных mymsgput (на q->empty)
    int i;
    int sem_val;
    sem_getvalue(&q->empty, &sem_val);
    int num_to_post_put = MAX_QUEUE_SIZE - sem_val; // Количество заблокированных на empty
    for (i = 0; i < num_to_post_put; i++) {
        sem_post(&q->empty);
    }
    
    // Разблокировка заблокированных mymsgget (на q->full)
    sem_getvalue(&q->full, &sem_val);
    int num_to_post_get = sem_val; // Количество заблокированных на full
    for (i = 0; i < num_to_post_get; i++) {
        sem_post(&q->full);
    }
    
    // Также можно разблокировать все пустые и полные слоты до максимума
    // (на случай, если нити блокируются сразу после drop, но до захвата мьютекса)
    // Этот метод более безопасный, гарантирует, что все post будут обработаны:
    for (i = 0; i < MAX_QUEUE_SIZE; i++) {
         sem_post(&q->full);
         sem_post(&q->empty);
    }


    pthread_mutex_unlock(&q->mutex);
    
    printf("\n>>> ОЧЕРЕДЬ СБРОШЕНА (DROP). Get/Put возвращают 0. <<<\n\n");
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: mymsgdestroy
НАЗНАЧЕНИЕ: Полное уничтожение очереди сообщений с освобождением всех ресурсов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    q - указатель на очередь сообщений для уничтожения
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_destroy() - уничтожение мьютекса
    sem_destroy() - уничтожение семафоров
    pthread_cond_destroy() - уничтожение условной переменной
    perror() - обработка ошибок при уничтожении синхронизационных примитивов
    printf() - уведомление об успешном уничтожении очереди
--------------------------------------------------
*/
void mymsgdestroy(queue_t *q) {
    // Уничтожение мьютекса
    if (pthread_mutex_destroy(&q->mutex) != 0) {
        perror("pthread_mutex_destroy failed");
    }
    
    // Уничтожение семафоров
    if (sem_destroy(&q->full) != 0) {
        perror("sem_destroy full failed");
    }
    if (sem_destroy(&q->empty) != 0) {
        perror("sem_destroy empty failed");
    }
    
    // Уничтожение условной переменной
    if (pthread_cond_destroy(&q->drop_cond) != 0) {
        perror("pthread_cond_destroy failed");
    }
    
    printf("Очередь уничтожена.\n");
}
