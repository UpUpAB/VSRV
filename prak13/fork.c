/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 03.12.25
ПРАКТИЧЕСКАЯ РАБОТА №13
ИМЯ ФАЙЛА: fork.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация решения задачи об обедающих философах с использованием атомарного захвата
вилок и условных переменных. Реализация стратегии, при которой философ либо захватывает
обе вилки одновременно, либо освобождает все захваченные вилки и ожидает.
Исследование предотвращения deadlock через координацию доступа к вилкам с помощью
глобального мьютекса и условной переменной для оповещения об освобождении ресурсов.
--------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define PHILOSOPHERS_COUNT 5

/*
--------------------------------------------------
ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ:
forks - массив мьютексов, представляющих вилки
forks_mutex - глобальный мьютекс для атомарного захвата вилок
forks_cond - условная переменная для оповещения об освобождении вилок
philosophers - массив идентификаторов потоков философов
philosopher_ids - массив идентификаторов философов
--------------------------------------------------
*/
pthread_mutex_t forks[PHILOSOPHERS_COUNT];
pthread_mutex_t forks_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t forks_cond = PTHREAD_COND_INITIALIZER;
pthread_t philosophers[PHILOSOPHERS_COUNT];
int philosopher_ids[PHILOSOPHERS_COUNT];

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: philosopher
НАЗНАЧЕНИЕ: Функция потока философа с атомарным захватом вилок
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    arg - указатель на идентификатор философа
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват глобального мьютекса для атомарной проверки вилок
    pthread_mutex_unlock() - освобождение глобального мьютекса
    pthread_mutex_trylock() - неблокирующая попытка захвата вилки
    pthread_cond_wait() - ожидание освобождения вилок на условной переменной
    pthread_cond_broadcast() - оповещение всех философов об освобождении вилок
    usleep() - имитация времени размышления и приема пищи
    printf() - вывод информации о состоянии философа
    rand() - генерация случайных временных интервалов
--------------------------------------------------
*/
void* philosopher(void* arg) {
    int id = *(int*)arg;
    int left_fork = id;
    int right_fork = (id + 1) % PHILOSOPHERS_COUNT;
    
    while (1) {
        // Философ размышляет
        printf("Философ %d размышляет...\n", id);
        usleep(rand() % 1000000 + 500000);
        
        // Пытается взять вилки атомарно
        printf("Философ %d хочет есть\n", id);
        
        pthread_mutex_lock(&forks_mutex);
        
        int success = 0;
        while (!success) {
            // Пытаемся захватить левую вилку
            if (pthread_mutex_trylock(&forks[left_fork]) == 0) {
                // Пытаемся захватить правую вилку
                if (pthread_mutex_trylock(&forks[right_fork]) == 0) {
                    success = 1;
                    printf("Философ %d взял обе вилки и начинает есть\n", id);
                } else {
                    // Не удалось взять правую вилку - отпускаем левую
                    pthread_mutex_unlock(&forks[left_fork]);
                }
            }
            
            if (!success) {
                // Ждем, пока вилки освободятся
                pthread_cond_wait(&forks_cond, &forks_mutex);
            }
        }
        
        pthread_mutex_unlock(&forks_mutex);
        
        // Философ ест
        usleep(rand() % 1000000 + 500000);
        printf("Философ %d поел и кладет вилки\n", id);
        
        // Кладет вилки и оповещает других
        pthread_mutex_lock(&forks_mutex);
        pthread_mutex_unlock(&forks[left_fork]);
        pthread_mutex_unlock(&forks[right_fork]);
        pthread_cond_broadcast(&forks_cond);
        pthread_mutex_unlock(&forks_mutex);
    }
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, инициализирующая систему обедающих философов
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_init() - инициализация мьютексов вилок
    pthread_create() - создание потоков философов
    pthread_join() - ожидание завершения потоков философов (в данной версии бесконечное)
    pthread_mutex_destroy() - уничтожение мьютексов
    pthread_cond_destroy() - уничтожение условной переменной
    srand() - инициализация генератора случайных чисел
    time() - получение текущего времени для инициализации генератора
--------------------------------------------------
*/
int main() {
    srand(time(NULL));
    
    // Инициализация мьютексов (вилок)
    for (int i = 0; i < PHILOSOPHERS_COUNT; i++) {
        pthread_mutex_init(&forks[i], NULL);
    }
    
    // Создание потоков философов
    for (int i = 0; i < PHILOSOPHERS_COUNT; i++) {
        philosopher_ids[i] = i;
        pthread_create(&philosophers[i], NULL, philosopher, &philosopher_ids[i]);
    }
    
    // Ожидание завершения
    for (int i = 0; i < PHILOSOPHERS_COUNT; i++) {
        pthread_join(philosophers[i], NULL);
    }
    // Уничтожение мьютексов
    for (int i = 0; i < PHILOSOPHERS_COUNT; i++) {
        pthread_mutex_destroy(&forks[i]);
    }
    pthread_mutex_destroy(&forks_mutex);
    pthread_cond_destroy(&forks_cond);
    return 0;
}
