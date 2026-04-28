/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 02.12.25
ПРАКТИЧЕСКАЯ РАБОТА №13
ИМЯ ФАЙЛА: mul_pot.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация многопоточного взаимодействия с условными переменными для синхронизации работы.
Реализация модели производитель-потребитель с двумя типами потребителей, обрабатывающих
данные в зависимости от их свойств (четность числа).
Исследование механизмов pthread_cond_wait() и pthread_cond_signal() для координации потоков.
--------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

/*
--------------------------------------------------
СТРУКТУРА: SharedData
НАЗНАЧЕНИЕ: Структура для хранения общих данных, разделяемых между потоками
ПОЛЯ:
    value - текущее сгенерированное число
    processed - флаг обработки числа (0 - не обработано, 1 - обработано)
    data_lock - мьютекс для синхронизации доступа к данным
    data_ready - условная переменная для сигнализации о готовности данных
--------------------------------------------------
*/
typedef struct {
    int value;
    int processed;
    pthread_mutex_t data_lock;
    pthread_cond_t data_ready;
} SharedData;

/*
--------------------------------------------------
СТРУКТУРА: ThreadInfo
НАЗНАЧЕНИЕ: Структура для передачи информации в потоки
ПОЛЯ:
    id - идентификатор потока
    data_ptr - указатель на общие данные
--------------------------------------------------
*/
typedef struct {
    int id;
    SharedData* data_ptr;
} ThreadInfo;

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: power
НАЗНАЧЕНИЕ: Функция вычисления степени числа (возведение в степень)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    base - основание степени
    exponent - показатель степени
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    Результат возведения base в степень exponent
--------------------------------------------------
*/
static inline int power(int base, int exponent) {
    int result = 1;
    for (int i = 0; i < exponent; i++) {
        result *= base;
    }
    return result;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: number_producer
НАЗНАЧЕНИЕ: Поток-источник, генерирующий случайные числа каждую секунду
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на структуру ThreadInfo с информацией о потоке
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для работы с общими данными
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_signal() - сигнализация о готовности новых данных
    rand_r() - генерация случайных чисел с потокобезопасным состоянием
    time() - получение текущего времени для инициализации генератора
    nanosleep() - точная задержка между генерациями чисел
    printf() - вывод информации о сгенерированных числах
--------------------------------------------------
*/
void* number_producer(void* param) {
    ThreadInfo* info = (ThreadInfo*)param;
    SharedData* shared = info->data_ptr;
    
    unsigned int seed = time(NULL) ^ info->id;
    
    while (1) {
        pthread_mutex_lock(&shared->data_lock);
        
        /* Генерация числа с диапазоном 1-50 */
        shared->value = (rand_r(&seed) % 50) + 1;
        shared->processed = 0;
        
        printf("[Источник] Создано число: %d\n", shared->value);
        
        /* Сигнализируем о готовности данных */
        pthread_cond_signal(&shared->data_ready);
        pthread_mutex_unlock(&shared->data_lock);
        
        /* Пауза между генерациями */
        struct timespec interval = {1, 0};
        nanosleep(&interval, NULL);
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: even_number_handler
НАЗНАЧЕНИЕ: Поток для обработки четных чисел (вычисление квадрата)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на структуру ThreadInfo с информацией о потоке
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к общим данным
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание поступления четного числа
    power() - вычисление квадрата числа
    printf() - вывод результата вычисления
--------------------------------------------------
*/
void* even_number_handler(void* param) {
    ThreadInfo* info = (ThreadInfo*)param;
    SharedData* shared = info->data_ptr;
    
    while (1) {
        pthread_mutex_lock(&shared->data_lock);
        
        /* Ожидание четного непроцессированного числа */
        while (shared->processed || (shared->value % 2 != 0)) {
            pthread_cond_wait(&shared->data_ready, &shared->data_lock);
        }
        
        if (!shared->processed && (shared->value % 2 == 0)) {
            int result = power(shared->value, 2);
            printf("[Обработчик четных] Число %d → квадрат = %d\n",
                   shared->value, result);
            shared->processed = 1;
        }
        
        pthread_mutex_unlock(&shared->data_lock);
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: odd_number_handler
НАЗНАЧЕНИЕ: Поток для обработки нечетных чисел (вычисление куба)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на структуру ThreadInfo с информацией о потоке
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к общим данным
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание поступления нечетного числа
    power() - вычисление куба числа
    printf() - вывод результата вычисления
--------------------------------------------------
*/
void* odd_number_handler(void* param) {
    ThreadInfo* info = (ThreadInfo*)param;
    SharedData* data = info->data_ptr;
    
    while (1) {
        pthread_mutex_lock(&data->data_lock);
        
        /* Ожидание нечетного непроцессированного числа */
        while (data->processed || (data->value % 2 == 0)) {
            pthread_cond_wait(&data->data_ready, &data->data_lock);
        }
        
        if (!data->processed && (data->value % 2 != 0)) {
            int cubic_result = power(data->value, 3);
            printf("[Обработчик нечетных] Число %d → куб = %d\n",
                   data->value, cubic_result);
            data->processed = 1;
        }
        
        pthread_mutex_unlock(&data->data_lock);
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: init_shared_data
НАЗНАЧЕНИЕ: Инициализация общих данных и синхронизационных примитивов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    data - указатель на структуру SharedData для инициализации
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_init() - инициализация мьютекса
    pthread_cond_init() - инициализация условной переменной
--------------------------------------------------
*/
void init_shared_data(SharedData* data) {
    data->value = 0;
    data->processed = 1; /* Начальное состояние - данных нет */
    pthread_mutex_init(&data->data_lock, NULL);
    pthread_cond_init(&data->data_ready, NULL);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: cleanup
НАЗНАЧЕНИЕ: Освобождение ресурсов и уничтожение синхронизационных примитивов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    data - указатель на структуру SharedData для очистки
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_destroy() - уничтожение мьютекса
    pthread_cond_destroy() - уничтожение условной переменной
--------------------------------------------------
*/
void cleanup(SharedData* data) {
    pthread_mutex_destroy(&data->data_lock);
    pthread_cond_destroy(&data->data_ready);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, инициализирующая и координирующая работу потоков
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание трех потоков: источника, обработчика четных и нечетных чисел
    init_shared_data() - инициализация общих данных
    sleep() - ожидание в основном потоке для демонстрации работы системы
    cleanup() - очистка ресурсов перед завершением
    printf() - вывод информации о работе системы
--------------------------------------------------
*/
int main(void) {
    pthread_t producer_thread, even_thread, odd_thread;
    SharedData common_data;
    ThreadInfo thread_infos[3];
    
    printf("=== Многопоточная система обработки чисел ===\n");
    printf("Формат вывода: [Поток] Действие\n\n");
    
    /* Инициализация общих данных */
    init_shared_data(&common_data);
    
    /* Подготовка информации для потоков */
    for (int i = 0; i < 3; i++) {
        thread_infos[i].id = i;
        thread_infos[i].data_ptr = &common_data;
    }
    
    /* Создание потоков */
    pthread_create(&producer_thread, NULL, number_producer, &thread_infos[0]);
    pthread_create(&even_thread, NULL, even_number_handler, &thread_infos[1]);
    pthread_create(&odd_thread, NULL, odd_number_handler, &thread_infos[2]);
    
    /* Основной цикл работы (10 секунд для демонстрации) */
    for (int i = 0; i < 10; i++) {
        sleep(1);
    }
    
    printf("\n=== Завершение работы системы ===\n");
    
    /* В реальной программе здесь была бы корректная остановка потоков */
    /* Для демонстрации просто выходим */
    
    cleanup(&common_data);
    
    return EXIT_SUCCESS;
}
