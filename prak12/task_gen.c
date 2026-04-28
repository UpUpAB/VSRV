/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 01.12.25
ПРАКТИЧЕСКАЯ РАБОМА №12
ИМЯ ФАЙЛА: task_gen.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация модели производитель-потребитель с использованием условных переменных.
Реализация многопоточного сервиса обработки уведомлений с синхронизацией через мьютексы и условные переменные.
Исследование механизмов pthread_cond_wait() и pthread_cond_signal() для координации потоков.
--------------------------------------------------
*/

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define MAX_PENDING_ITEMS 100
#define PROCESSING_UNITS 3

/*
--------------------------------------------------
СТРУКТУРА: work_item
НАЗНАЧЕНИЕ: Структура для хранения элементов работы в очереди
ПОЛЯ:
    identifier - уникальный идентификатор элемента
    description - текстовое описание задания
    subsequent - указатель на следующий элемент в очереди
--------------------------------------------------
*/
typedef struct work_item {
    int identifier;
    char description[256];
    struct work_item* subsequent;
} work_item;

/*
--------------------------------------------------
ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ:
access_guard - мьютекс для синхронизации доступа к очереди
work_present - условная переменная для сигнализации о наличии работы
pending_list - голова очереди ожидающих заданий
list_tail - хвост очереди для эффективного добавления
pending_count - количество элементов в очереди
id_generator - генератор уникальных идентификаторов
--------------------------------------------------
*/
pthread_mutex_t access_guard = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_present = PTHREAD_COND_INITIALIZER;

work_item* pending_list = NULL;
work_item* list_tail = NULL;
int pending_count = 0;
int id_generator = 0;

/*
--------------------------------------------------
МАССИВЫ ШАБЛОНОВ:
notification_templates - массив шаблонов для генерации уведомлений
template_count - количество доступных шаблонов
--------------------------------------------------
*/
const char* notification_templates[] = {
    "Registration confirmation",
    "Security code delivery",
    "Transaction verification",
    "Service announcement",
    "Profile update alert",
    "Billing statement",
    "Platform notification",
    "Promotional content"
};
const int template_count = sizeof(notification_templates) / sizeof(notification_templates[0]);

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: submit_work
НАЗНАЧЕНИЕ: Добавление нового задания в очередь ожидания
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    content - текстовое описание задания
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для работы с очередью
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_signal() - сигнализация о наличии новых заданий
    malloc() - выделение памяти для нового элемента
    strncpy() - копирование описания задания
    fprintf() - вывод информации о добавлении задания
--------------------------------------------------
*/
void submit_work(const char* content) {
    pthread_mutex_lock(&access_guard);
    
    if (pending_count >= MAX_PENDING_ITEMS) {
        fprintf(stdout, "Capacity limit reached! Unable to accept additional items.\n");
        pthread_mutex_unlock(&access_guard);
        return;
    }
    
    work_item* new_item = (work_item*)malloc(sizeof(work_item));
    if (!new_item) {
        fprintf(stderr, "Resource allocation unsuccessful\n");
        pthread_mutex_unlock(&access_guard);
        return;
    }
    
    new_item->identifier = id_generator++;
    strncpy(new_item->description, content, 255);
    new_item->description[255] = '\0';
    new_item->subsequent = NULL;
    
    if (pending_list == NULL) {
        pending_list = new_item;
        list_tail = new_item;
    } else {
        list_tail->subsequent = new_item;
        list_tail = new_item;
    }
    
    pending_count++;
    
    fprintf(stdout, "Generator: Submitted item %d - '%s' (Queue: %d)\n",
            new_item->identifier, new_item->description, pending_count);
    
    pthread_cond_signal(&work_present);
    pthread_mutex_unlock(&access_guard);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: retrieve_work
НАЗНАЧЕНИЕ: Извлечение задания из очереди ожидания
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    Указатель на извлеченный элемент или NULL при пустой очереди
--------------------------------------------------
*/
work_item* retrieve_work() {
    work_item* item = NULL;
    
    if (pending_count > 0) {
        item = pending_list;
        pending_list = item->subsequent;
        
        if (pending_list == NULL) {
            list_tail = NULL;
        }
        
        pending_count--;
    }
    
    return item;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: execute_work
НАЗНАЧЕНИЕ: Выполнение задания с имитацией обработки
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    item - указатель на задание для выполнения
    processor_id - идентификатор процессора для логирования
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    fprintf() - вывод информации о выполнении задания
    usleep() - имитация времени обработки
    free() - освобождение памяти после выполнения задания
--------------------------------------------------
*/
void execute_work(work_item* item, int processor_id) {
    if (item) {
        fprintf(stdout, "Processor %d: Handling item %d - '%s'\n",
                processor_id, item->identifier, item->description);
        
        usleep(150000 + (rand() % 250000));
        
        fprintf(stdout, "Processor %d: Finalized item %d\n", processor_id, item->identifier);
        
        free(item);
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: processing_unit
НАЗНАЧЕНИЕ: Функция потока-обработчика (потребитель)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на идентификатор обработчика
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к очереди
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание поступления новых заданий
    retrieve_work() - извлечение задания из очереди
    execute_work() - выполнение извлеченного задания
    fprintf() - вывод информации о состоянии обработчика
--------------------------------------------------
*/
void* processing_unit(void* param) {
    int unit_id = *((int*)param);
    work_item* current_item = NULL;
    
    fprintf(stdout, "Processing unit %d activated\n", unit_id);
    
    while (1) {
        pthread_mutex_lock(&access_guard);
        
        while (pending_count == 0) {
            fprintf(stdout, "Unit %d: Awaiting assignments\n", unit_id);
            pthread_cond_wait(&work_present, &access_guard);
        }
        
        current_item = retrieve_work();
        
        pthread_mutex_unlock(&access_guard);
        
        if (current_item) {
            execute_work(current_item, unit_id);
        }
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: work_generator
НАЗНАЧЕНИЕ: Функция потока-генератора (производитель)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на идентификатор генератора
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    usleep() - случайная задержка между генерацией заданий
    submit_work() - добавление задания в очередь
    snprintf() - форматирование описания задания
    fprintf() - вывод информации о генерации задания
    rand() - генерация случайных значений
--------------------------------------------------
*/
void* work_generator(void* param) {
    int generator_id = *((int*)param);
    
    fprintf(stdout, "Generator unit %d activated\n", generator_id);
    
    for (int iteration = 0; iteration < 10; iteration++) {
        usleep(300000 + (rand() % 1700000));
        
        const char* base_template = notification_templates[rand() % template_count];
        char complete_description[256];
        snprintf(complete_description, sizeof(complete_description),
                "%s [Source: Generator %d]", base_template, generator_id);
        
        submit_work(complete_description);
    }
    
    fprintf(stdout, "Generator unit %d completed\n", generator_id);
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, инициализирующая и координирующая работу потоков
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание потоков обработчиков и генераторов
    pthread_join() - ожидание завершения потоков-генераторов
    srand() - инициализация генератора случайных чисел
    time() - получение текущего времени для инициализации генератора
    sleep() - финальная пауза для завершения обработки
    fprintf() - вывод информации о состоянии системы
--------------------------------------------------
*/
int main() {
    pthread_t processors[PROCESSING_UNITS];
    pthread_t generators[2];
    int processor_ids[PROCESSING_UNITS];
    int generator_ids[2];
    
    srand(time(NULL));
    
    fprintf(stdout, "=== Distributed Processing System Initialized ===\n");
    
    for (int idx = 0; idx < PROCESSING_UNITS; idx++) {
        processor_ids[idx] = idx + 1;
        pthread_create(&processors[idx], NULL, processing_unit, &processor_ids[idx]);
    }
    
    for (int idx = 0; idx < 2; idx++) {
        generator_ids[idx] = idx + 1;
        pthread_create(&generators[idx], NULL, work_generator, &generator_ids[idx]);
    }
    
    for (int idx = 0; idx < 2; idx++) {
        pthread_join(generators[idx], NULL);
    }
    
    sleep(2);
    
    fprintf(stdout, "=== Processing Cycle Completed ===\n");
    fprintf(stdout, "Remaining pending items: %d\n", pending_count);
    
    return 0;
}
