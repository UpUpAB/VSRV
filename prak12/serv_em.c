/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 01.12.25
ПРАКТИЧЕСКАЯ РАБОТА №12
ИМЯ ФАЙЛА: serv_em.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация оптимизации многопоточной системы с разделением блокировок для разных ресурсов.
Реализация модели производитель-потребитель с использованием двух отдельных мьютексов:
один для защиты очереди задач, второй для синхронизации условных переменных.
Исследование производительности и корректности работы системы при разделении блокировок.
--------------------------------------------------
*/

#include <stdio.h>
#include <unistd.h>
#define __USE_GNU
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

/* Конфигурация потоков обработчиков */
#define WORKER_THREAD_COUNT 3
#define MAX_REQUESTS 20

/*
--------------------------------------------------
ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ:
request_guard - мьютекс для защиты доступа к списку задач
signal_lock - мьютекс для защиты механизма сигнализации
task_available - условная переменная для оповещения о новых задачах
active_tasks - счетчик активных задач в системе
--------------------------------------------------
*/
pthread_mutex_t request_guard = PTHREAD_MUTEX_INITIALIZER;  /* Защита списка задач */
pthread_mutex_t signal_lock = PTHREAD_MUTEX_INITIALIZER;    /* Защита сигнального механизма */

/* Глобальный сигнал о появлении задач */
pthread_cond_t task_available = PTHREAD_COND_INITIALIZER;

/* Счетчик активных задач */
int active_tasks = 0;

/*
--------------------------------------------------
СТРУКТУРА: Task
НАЗНАЧЕНИЕ: Структура для хранения задачи в односвязном списке
ПОЛЯ:
    identifier - уникальный идентификатор задачи
    following - указатель на следующую задачу в очереди
--------------------------------------------------
*/
typedef struct task_node {
    int identifier;
    struct task_node* following;
} Task;

/* Указатели на начало и конец списка */
Task* task_queue_start = NULL;
Task* task_queue_end = NULL;

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: enqueue_task
НАЗНАЧЕНИЕ: Создание и добавление новой задачи в очередь с использованием раздельных мьютексов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    task_id - уникальный идентификатор добавляемой задачи
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к очереди задач
    pthread_mutex_unlock() - освобождение мьютекса
    malloc() - выделение памяти для новой задачи
    pthread_cond_signal() - оповещение потоков о новой задаче
    perror() - обработка ошибок выделения памяти
    exit() - аварийное завершение при критической ошибке
    fprintf() - вывод отладочной информации
--------------------------------------------------
*/
void enqueue_task(int task_id)
{
    Task* new_task = (Task*)malloc(sizeof(Task));
    
    if (new_task == NULL) {
        perror("Ошибка выделения памяти для задачи");
        exit(EXIT_FAILURE);
    }
    
    new_task->identifier = task_id;
    new_task->following = NULL;

    /* Блокируем доступ к очереди */
    pthread_mutex_lock(&request_guard);
    
    if (active_tasks == 0) {
        task_queue_start = new_task;
        task_queue_end = new_task;
    } else {
        task_queue_end->following = new_task;
        task_queue_end = new_task;
    }
    
    active_tasks++;

    #ifdef DEBUG_MODE
    fprintf(stderr, "Добавлена задача ID: %d\n", task_id);
    #endif
    
    pthread_mutex_unlock(&request_guard);
    
    /* Оповещаем потоки о новой задаче */
    pthread_mutex_lock(&signal_lock);
    pthread_cond_signal(&task_available);
    pthread_mutex_unlock(&signal_lock);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: fetch_next_task
НАЗНАЧЕНИЕ: Извлечение задачи из очереди с защитой отдельным мьютексом
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    Указатель на извлеченную задачу или NULL при пустой очереди
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к очереди
    pthread_mutex_unlock() - освобождение мьютекса
--------------------------------------------------
*/
Task* fetch_next_task(void)
{
    Task* current_task = NULL;
    
    pthread_mutex_lock(&request_guard);
    
    if (active_tasks > 0) {
        current_task = task_queue_start;
        task_queue_start = current_task->following;
        
        if (task_queue_start == NULL) {
            task_queue_end = NULL;
        }
        
        active_tasks--;
    }
    
    pthread_mutex_unlock(&request_guard);
    
    return current_task;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: execute_task
НАЗНАЧЕНИЕ: Обработка отдельной задачи с выводом информации
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    task - указатель на задачу для выполнения
    worker_id - идентификатор работника для логирования
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    fprintf() - вывод информации о выполнении задачи
    fflush() - принудительный вывод буфера
--------------------------------------------------
*/
void execute_task(Task* task, int worker_id)
{
    if (task != NULL) {
        fprintf(stdout, "Работник %d выполнил задачу %d\n", worker_id, task->identifier);
        fflush(stdout);
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: worker_thread_routine
НАЗНАЧЕНИЕ: Основной цикл работы потока-обработчика с использованием раздельных мьютексов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на идентификатор потока
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    fetch_next_task() - извлечение задачи из очереди
    execute_task() - выполнение извлеченной задачи
    free() - освобождение памяти после выполнения задачи
    pthread_mutex_lock() - захват мьютекса сигнализации
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание новых задач при пустой очереди
    fprintf() - вывод отладочной информации
--------------------------------------------------
*/
void* worker_thread_routine(void* param)
{
    int my_id = *((int*)param);
    Task* current_task = NULL;
    int check_attempts = 0;
    
    #ifdef DEBUG_MODE
    fprintf(stderr, "Поток %d начал работу\n", my_id);
    #endif
    
    while (1) {
        current_task = fetch_next_task();
        
        if (current_task != NULL) {
            execute_task(current_task, my_id);
            free(current_task);
            check_attempts = 0;
        } else {
            if (check_attempts < 2) {
                check_attempts++;
                continue;
            }
            
            pthread_mutex_lock(&signal_lock);
            
            /* Повторная проверка перед ожиданием */
            current_task = fetch_next_task();
            if (current_task == NULL) {
                #ifdef DEBUG_MODE
                fprintf(stderr, "Поток %d переходит в ожидание\n", my_id);
                #endif
                
                pthread_cond_wait(&task_available, &signal_lock);
                
                #ifdef DEBUG_MODE
                fprintf(stderr, "Поток %d возобновил работу\n", my_id);
                #endif
            }
            
            pthread_mutex_unlock(&signal_lock);
            
            if (current_task != NULL) {
                execute_task(current_task, my_id);
                free(current_task);
            }
        }
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: random_delay
НАЗНАЧЕНИЕ: Функция генерации случайной задержки для имитации реальной работы
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    rand() - генерация случайного числа
    nanosleep() - выполнение задержки с наносекундной точностью
--------------------------------------------------
*/
void random_delay(void)
{
    struct timespec pause_time;
    pause_time.tv_sec = 0;
    pause_time.tv_nsec = (rand() % 20 + 5) * 1000000; /* 5-25ms */
    nanosleep(&pause_time, NULL);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, демонстрирующая работу системы с разделенными мьютексами
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание потоков-обработчиков
    pthread_join() - ожидание завершения потоков (в данной версии не используется)
    enqueue_task() - добавление задач в очередь
    random_delay() - генерация случайных задержек
    srand() - инициализация генератора случайных чисел
    time() - получение текущего времени для инициализации генератора
    sleep() - временные задержки для синхронизации
    printf() - вывод информации о работе системы
    perror() - обработка ошибок создания потоков
--------------------------------------------------
*/
int main(void)
{
    int thread_ids[WORKER_THREAD_COUNT];
    pthread_t workers[WORKER_THREAD_COUNT];
    int task_counter;
    
    /* Инициализация генератора случайных чисел */
    srand(time(NULL));
    
    printf("Сервер запущен с %d потоками-обработчиками\n", WORKER_THREAD_COUNT);
    printf("Используется раздельная блокировка для очереди и сигналов\n\n");
    
    /* Создание рабочих потоков */
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        thread_ids[i] = i;
        if (pthread_create(&workers[i], NULL, worker_thread_routine, &thread_ids[i]) != 0) {
            perror("Ошибка создания потока");
            return EXIT_FAILURE;
        }
    }
    
    /* Даем потокам время на инициализацию */
    sleep(2);
    
    printf("Генерация задач...\n");
    
    /* Генерация задач */
    for (task_counter = 0; task_counter < MAX_REQUESTS; task_counter++) {
        enqueue_task(task_counter);
        
        /* Случайная задержка для имитации реальной работы */
        if (rand() % 4 == 0) {
            random_delay();
        }
    }
    
    /* Ожидание обработки всех задач */
    sleep(4);
    
    printf("\nВсе задачи успешно обработаны\n");
    printf("Осталось задач в очереди: %d\n", active_tasks);
    printf("Завершение работы сервера\n");
    
    return EXIT_SUCCESS;
}
