/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 02.12.25
ПРАКТИЧЕСКАЯ РАБОТА №13
ИМЯ ФАЙЛА: cat.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация многопоточного приложения с распределенной синхронизацией потоков.
Реализация модели "производитель-потребитель" с одним производителем (владелец) и
несколькими потребителями (коты) с ограничением емкости ресурса.
Исследование механизмов условных переменных и мьютексов для управления доступом
к общему ресурсу (кормушке) с заданными граничными условиями.
--------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

/* Конфигурация системы */
#define BOWL_CAPACITY_MAX 1000
#define BOWL_REFILL_THRESHOLD 100
#define FELINE_COUNT 5
#define MEAL_PORTION 100
#define SIMULATION_DURATION 30  /* секунд работы программы */

/*
--------------------------------------------------
СТРУКТУРА: FeedingStation
НАЗНАЧЕНИЕ: Структура для управления кормушкой и синхронизации потоков
ПОЛЯ:
    access_lock - мьютекс для защиты доступа к общим данным кормушки
    food_supplied - условная переменная для сигнализации о наличии корма
    refill_needed - условная переменная для сигнализации о необходимости пополнения
    current_food - текущее количество корма в кормушке
    simulation_active - флаг активности симуляции
--------------------------------------------------
*/
typedef struct {
    pthread_mutex_t access_lock;
    pthread_cond_t food_supplied;
    pthread_cond_t refill_needed;
    int current_food;
    bool simulation_active;
} FeedingStation;

/*
--------------------------------------------------
СТРУКТУРА: FelineData
НАЗНАЧЕНИЕ: Структура для хранения данных о коте
ПОЛЯ:
    identifier - уникальный идентификатор кота
    meals_consumed - количество съеденных порций
    station - указатель на кормушку
--------------------------------------------------
*/
typedef struct {
    int identifier;
    int meals_consumed;
    FeedingStation* station;
} FelineData;

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: init_feeding_station
НАЗНАЧЕНИЕ: Инициализация кормушки и синхронизационных примитивов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    station - указатель на кормушку для инициализации
    initial_food - начальное количество корма в кормушке
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_init() - инициализация мьютекса
    pthread_cond_init() - инициализация условных переменных
--------------------------------------------------
*/
void init_feeding_station(FeedingStation* station, int initial_food) {
    station->current_food = initial_food;
    station->simulation_active = true;
    pthread_mutex_init(&station->access_lock, NULL);
    pthread_cond_init(&station->food_supplied, NULL);
    pthread_cond_init(&station->refill_needed, NULL);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: cleanup_feeding_station
НАЗНАЧЕНИЕ: Освобождение ресурсов кормушки и уничтожение синхронизационных примитивов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    station - указатель на кормушку для очистки
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_destroy() - уничтожение мьютекса
    pthread_cond_destroy() - уничтожение условных переменных
--------------------------------------------------
*/
void cleanup_feeding_station(FeedingStation* station) {
    pthread_mutex_destroy(&station->access_lock);
    pthread_cond_destroy(&station->food_supplied);
    pthread_cond_destroy(&station->refill_needed);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: caretaker_routine
НАЗНАЧЕНИЕ: Функция потока владельца, пополняющего корм в кормушке
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на кормушку (FeedingStation)
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к кормушке
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание сигнала о необходимости пополнения
    pthread_cond_broadcast() - оповещение всех котов о наличии корма
    nanosleep() - пауза между проверками состояния кормушки
    fprintf() - вывод информации о пополнении корма
--------------------------------------------------
*/
void* caretaker_routine(void* param) {
    FeedingStation* feeder = (FeedingStation*)param;
    
    while (feeder->simulation_active) {
        pthread_mutex_lock(&feeder->access_lock);
        
        /* Ожидание сигнала о необходимости пополнения */
        while (feeder->current_food > BOWL_REFILL_THRESHOLD &&
               feeder->simulation_active) {
            pthread_cond_wait(&feeder->refill_needed, &feeder->access_lock);
        }
        
        if (feeder->simulation_active) {
            int previous_amount = feeder->current_food;
            int refill_amount = BOWL_CAPACITY_MAX - previous_amount;
            
            feeder->current_food = BOWL_CAPACITY_MAX;
            
            fprintf(stdout, "[Хозяин] Пополнил кормушку на %dг (было %dг, стало %dг)\n",
                    refill_amount, previous_amount, feeder->current_food);
            
            /* Оповещение всех котов о наличии еды */
            pthread_cond_broadcast(&feeder->food_supplied);
        }
        
        pthread_mutex_unlock(&feeder->access_lock);
        
        /* Отдых между проверками */
        if (feeder->simulation_active) {
            struct timespec rest = {2, 0};
            nanosleep(&rest, NULL);
        }
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: feline_routine
НАЗНАЧЕНИЕ: Функция потока кота, потребляющего корм из кормушки
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на структуру FelineData с данными кота
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к кормушке
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание достаточного количества корма
    pthread_cond_signal() - сигнализация владельцу о необходимости пополнения
    rand_r() - генерация случайного времени переваривания
    sleep() - имитация времени переваривания пищи
    fprintf() - вывод информации о потреблении корма
--------------------------------------------------
*/
void* feline_routine(void* param) {
    FelineData* feline_info = (FelineData*)param;
    FeedingStation* feeder = feline_info->station;
    
    while (feeder->simulation_active) {
        pthread_mutex_lock(&feeder->access_lock);
        
        /* Ожидание достаточного количества корма */
        while (feeder->current_food < MEAL_PORTION &&
               feeder->simulation_active) {
            
            if (feeder->current_food <= BOWL_REFILL_THRESHOLD) {
                pthread_cond_signal(&feeder->refill_needed);
            }
            
            pthread_cond_wait(&feeder->food_supplied, &feeder->access_lock);
        }
        
        if (feeder->simulation_active && feeder->current_food >= MEAL_PORTION) {
            feeder->current_food -= MEAL_PORTION;
            feline_info->meals_consumed++;
            
            fprintf(stdout, "[Кот %d] Съел порцию %dг. Остаток: %dг (Всего съедено: %d порций)\n",
                    feline_info->identifier, MEAL_PORTION,
                    feeder->current_food, feline_info->meals_consumed);
            
            /* Сигнал хозяину при необходимости пополнения */
            if (feeder->current_food <= BOWL_REFILL_THRESHOLD) {
                pthread_cond_signal(&feeder->refill_needed);
            }
        }
        
        pthread_mutex_unlock(&feeder->access_lock);
        
        /* Время на переваривание */
        if (feeder->simulation_active) {
            unsigned int seed = time(NULL) ^ feline_info->identifier;
            int digestion_time = (rand_r(&seed) % 3) + 1;
            sleep(digestion_time);
        }
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: generate_report
НАЗНАЧЕНИЕ: Генерация отчета о работе системы кормления
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    felines - массив структур с данными о котах
    feline_count - количество котов
    station - указатель на кормушку
    duration - длительность симуляции в секундах
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    printf() - вывод статистической информации о работе системы
--------------------------------------------------
*/
void generate_report(FelineData* felines, int feline_count,
                     FeedingStation* station, int duration) {
    printf("\n=== ОТЧЕТ О РАБОТЕ СИСТЕМЫ КОРМЛЕНИЯ ===\n");
    printf("Длительность симуляции: %d секунд\n", duration);
    printf("Конечное количество корма: %dг\n", station->current_food);
    printf("Количество питомцев: %d\n\n", feline_count);
    
    int total_meals = 0;
    printf("Статистика по котам:\n");
    for (int i = 0; i < feline_count; i++) {
        printf("  Кот %d: съел %d порций\n",
               felines[i].identifier, felines[i].meals_consumed);
        total_meals += felines[i].meals_consumed;
    }
    
    printf("\nИтого:\n");
    printf("  Всего съедено порций: %d\n", total_meals);
    printf("  Всего съедено корма: %dг\n", total_meals * MEAL_PORTION);
    printf("  Среднее на кота: %.1f порций\n",
           (float)total_meals / feline_count);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, координирующая работу системы кормления
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание потоков владельца и котов
    pthread_join() - ожидание завершения всех потоков
    init_feeding_station() - инициализация кормушки
    cleanup_feeding_station() - освобождение ресурсов кормушки
    generate_report() - генерация отчета о работе системы
    srand() - инициализация генератора случайных чисел
    time() - получение текущего времени
    sleep() - задание времени работы симуляции
    pthread_mutex_lock() - блокировка доступа для изменения флага завершения
    pthread_cond_broadcast() - оповещение всех потоков о завершении
    pthread_cond_signal() - сигнализация о завершении
    perror() - обработка ошибок создания потоков
    printf() - вывод информации о конфигурации и ходе работы системы
--------------------------------------------------
*/
int main(void) {
    pthread_t caretaker;
    pthread_t felines[FELINE_COUNT];
    FelineData feline_info[FELINE_COUNT];
    FeedingStation main_feeder;
    
    /* Инициализация генератора случайных чисел */
    srand(time(NULL));
    
    printf("=== СИСТЕМА АВТОМАТИЧЕСКОГО КОРМЛЕНИЯ КОТОВ ===\n");
    printf("Конфигурация:\n");
    printf("  Ёмкость кормушки: %dг\n", BOWL_CAPACITY_MAX);
    printf("  Порог пополнения: %dг\n", BOWL_REFILL_THRESHOLD);
    printf("  Размер порции: %dг\n", MEAL_PORTION);
    printf("  Количество котов: %d\n", FELINE_COUNT);
    printf("  Время работы: %d секунд\n\n", SIMULATION_DURATION);
    
    /* Инициализация кормушки */
    init_feeding_station(&main_feeder, 500);
    
    /* Создание потока владельца */
    if (pthread_create(&caretaker, NULL, caretaker_routine, &main_feeder) != 0) {
        perror("Ошибка создания потока владельца");
        return EXIT_FAILURE;
    }
    
    /* Создание потоков котов */
    for (int i = 0; i < FELINE_COUNT; i++) {
        feline_info[i].identifier = i + 1;
        feline_info[i].meals_consumed = 0;
        feline_info[i].station = &main_feeder;
        
        if (pthread_create(&felines[i], NULL, feline_routine, &feline_info[i]) != 0) {
            perror("Ошибка создания потока кота");
            return EXIT_FAILURE;
        }
    }
    
    /* Основное время работы программы */
    printf("Начало симуляции...\n");
    sleep(SIMULATION_DURATION);
    
    /* Корректное завершение потоков */
    printf("\nЗавершение симуляции...\n");
    pthread_mutex_lock(&main_feeder.access_lock);
    main_feeder.simulation_active = false;
    pthread_cond_broadcast(&main_feeder.food_supplied);
    pthread_cond_signal(&main_feeder.refill_needed);
    pthread_mutex_unlock(&main_feeder.access_lock);
    
    /* Ожидание завершения потоков */
    pthread_join(caretaker, NULL);
    for (int i = 0; i < FELINE_COUNT; i++) {
        pthread_join(felines[i], NULL);
    }
    
    /* Генерация отчета */
    generate_report(feline_info, FELINE_COUNT, &main_feeder, SIMULATION_DURATION);
    
    /* Освобождение ресурсов */
    cleanup_feeding_station(&main_feeder);
    
    printf("\nСистема завершила работу успешно.\n");
    return EXIT_SUCCESS;
}
