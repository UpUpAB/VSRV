/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 03.12.25
ПРАКТИЧЕСКАЯ РАБОТА №13
ИМЯ ФАЙЛА: parik.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация решения классической задачи "Спящий парикмахер" с использованием
мьютексов и условных переменных. Реализация модели производитель-потребитель, где
парикмахер (потребитель) обслуживает клиентов (производителей) с ограниченной
вместимостью приемной. Исследование механизмов синхронизации для управления
доступом к разделяемым ресурсам (рабочему месту парикмахера и стульям в приемной).
--------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define CHAIRS_COUNT 5
#define CUSTOMERS_COUNT 10

/*
--------------------------------------------------
ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ:
barber_mutex - мьютекс для синхронизации доступа к парикмахеру
customers_mutex - мьютекс для синхронизации доступа к очереди клиентов
barber_cond - условная переменная для сигнализации парикмахеру
customer_cond - условная переменная для сигнализации клиентам
waiting_customers - количество клиентов в приемной
barber_sleeping - флаг состояния парикмахера (1 - спит, 0 - работает)
served_customers - количество обслуженных клиентов
--------------------------------------------------
*/
pthread_mutex_t barber_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t customers_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t barber_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t customer_cond = PTHREAD_COND_INITIALIZER;

int waiting_customers = 0;
int barber_sleeping = 1;
int served_customers = 0;

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: barber
НАЗНАЧЕНИЕ: Функция потока парикмахера, обслуживающего клиентов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    arg - указатель на аргументы (не используется)
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса парикмахера
    pthread_mutex_unlock() - освобождение мьютекса парикмахера
    pthread_cond_wait() - ожидание появления клиентов при пустой очереди
    pthread_cond_signal() - оповещение клиента о начале стрижки
    usleep() - имитация времени стрижки
    printf() - вывод информации о состоянии парикмахера
    rand() - генерация случайного времени стрижки
--------------------------------------------------
*/
void* barber(void* arg) {
    while (served_customers < CUSTOMERS_COUNT) {
        pthread_mutex_lock(&barber_mutex);
        
        // Парикмахер спит, если нет клиентов
        if (waiting_customers == 0) {
            barber_sleeping = 1;
            printf("Парикмахер спит...\n");
            pthread_cond_wait(&customer_cond, &barber_mutex);
            barber_sleeping = 0;
            printf("Парикмахер проснулся и начинает работу\n");
        }
        
        // Обслуживание клиента
        waiting_customers--;
        served_customers++;
        printf("Парикмахер стрижет клиента. Ожидающих: %d\n", waiting_customers);
        
        // Оповещаем клиента, что начали стрижку
        pthread_cond_signal(&barber_cond);
        pthread_mutex_unlock(&barber_mutex);
        
        // Время стрижки
        usleep(rand() % 800000 + 500000);
        printf("Парикмахер закончил стрижку\n");
    }
    
    printf("Парикмахер завершил работу дня\n");
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: customer
НАЗНАЧЕНИЕ: Функция потока клиента, приходящего в парикмахерскую
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    arg - указатель на идентификатор клиента
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса очереди клиентов и парикмахера
    pthread_mutex_unlock() - освобождение мьютексов
    pthread_cond_wait() - ожидание своей очереди на стрижку
    pthread_cond_signal() - пробуждение парикмахера при необходимости
    usleep() - имитация времени прихода клиента
    printf() - вывод информации о состоянии клиента
    rand() - генерация случайного времени прихода
--------------------------------------------------
*/
void* customer(void* arg) {
    int id = *(int*)arg;
    
    usleep(rand() % 2000000); // Клиент приходит в случайное время
    
    pthread_mutex_lock(&customers_mutex);
    
    if (waiting_customers < CHAIRS_COUNT) {
        waiting_customers++;
        printf("Клиент %d пришел в парикмахерскую. Ожидающих: %d\n", id, waiting_customers);
        
        // Будим парикмахера, если он спит
        if (barber_sleeping) {
            pthread_cond_signal(&customer_cond);
        }
        
        pthread_mutex_unlock(&customers_mutex);
        
        // Ждем своей очереди
        pthread_mutex_lock(&barber_mutex);
        while (barber_sleeping || waiting_customers > 0) {
            pthread_cond_wait(&barber_cond, &barber_mutex);
        }
        pthread_mutex_unlock(&barber_mutex);
        
        printf("Клиент %d уходит подстриженным\n", id);
    } else {
        pthread_mutex_unlock(&customers_mutex);
        printf("Клиент %d ушел - нет свободных мест\n", id);
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, координирующая работу парикмахерской
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание потоков парикмахера и клиентов
    pthread_join() - ожидание завершения всех потоков
    srand() - инициализация генератора случайных чисел
    time() - получение текущего времени для инициализации генератора
    printf() - вывод итоговой информации о работе парикмахерской
--------------------------------------------------
*/
int main() {
    pthread_t barber_thread;
    pthread_t customer_threads[CUSTOMERS_COUNT];
    int customer_ids[CUSTOMERS_COUNT];
    
    srand(time(NULL));
    
    // Создаем поток парикмахера
    pthread_create(&barber_thread, NULL, barber, NULL);
    
    // Создаем потоки клиентов
    for (int i = 0; i < CUSTOMERS_COUNT; i++) {
        customer_ids[i] = i + 1;
        pthread_create(&customer_threads[i], NULL, customer, &customer_ids[i]);
    }
    
    // Ожидаем завершения всех клиентов
    for (int i = 0; i < CUSTOMERS_COUNT; i++) {
        pthread_join(customer_threads[i], NULL);
    }
    
    // Ожидаем завершения парикмахера
    pthread_join(barber_thread, NULL);
    
    printf("Все клиенты обслужены. Работа завершена.\n");
    
    return 0;
}
