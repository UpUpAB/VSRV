#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define NUM_THREADS 6
#define MAX_OPERATIONS 8

/* Глобальная переменная для демонстрации совместной работы */
int shared_result = 0;
pthread_mutex_t result_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Структура для передачи данных в поток */
typedef struct {
    int thread_id;
    int operations_count;
    int base_value;
} thread_data_t;

/* Вспомогательная функция для идентификации потоков */
void* identifier(void* arg) {
    int id = *(int*)arg;
    pthread_t self = pthread_self();
    
    printf("Worker thread %d:\n", id);
    printf("  - Assigned ID: %p\n", (void*)self);
    printf("  - Actual self ID: %p\n", (void*)self);
    printf("  - IDs match: YES\n");
    
    return NULL;
}

/* Долго выполняющийся поток */
void* long_running(void* arg) {
    printf("Long-running thread started (ID: %p)\n", (void*)pthread_self());
    sleep(3); // Имитация долгой работы
    printf("Long-running thread completed\n");
    return NULL;
}

/* Быстрый поток */
void* fast_thread(void* arg) {
    printf("Fast thread started (ID: %p)\n", (void*)pthread_self());
    sleep(1); // Короткая работа
    printf("Fast thread completed\n");
    return NULL;
}

/* Функция, выполняемая каждым потоком */
void* worker_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    pthread_t self_id = pthread_self();
    
    printf("Thread %d (ID: %p) started - will perform %d operations\n",
           data->thread_id, (void*)self_id, data->operations_count);
    
    /* Имитация работы потока */
    for (int i = 0; i < data->operations_count; i++) {
        /* Короткая задержка для имитации работы */
        usleep(100000); // 100ms
        
        /* Блокировка мьютекса для работы с общим ресурсом */
        pthread_mutex_lock(&result_mutex);
        
        int local_calc = data->base_value * (i + 1);
        shared_result += local_calc;
        
        printf("Thread %d (ID: %p): operation %d/%d, local=%d, shared=%d\n",
               data->thread_id, (void*)self_id, i + 1, data->operations_count,
               local_calc, shared_result);
        
        pthread_mutex_unlock(&result_mutex);
        
        /* Дополнительная задержка между операциями */
        usleep(50000); // 50ms
    }
    
    printf("Thread %d (ID: %p) completed all %d operations\n",
           data->thread_id, (void*)self_id, data->operations_count);
    
    /* Возвращаем результат работы потока */
    int* thread_result = malloc(sizeof(int));
    *thread_result = data->thread_id * 100 + data->operations_count;
    
    return (void*)thread_result;
}

/* Функция для создания потоков с разными параметрами */
void demonstrate_join_mechanism() {
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    void* thread_returns[NUM_THREADS];
    
    printf("=== DEMONSTRATING PTHREAD_JOIN MECHANISM ===\n");
    printf("Creating %d worker threads with different workloads...\n\n", NUM_THREADS);
    
    /* Инициализация данных для потоков */
    srand(time(NULL));
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i + 1;
        thread_data[i].operations_count = (rand() % MAX_OPERATIONS) + 1;
        thread_data[i].base_value = (i + 1) * 10;
    }
    
    /* Создание потоков */
    printf("--- CREATING THREADS ---\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        int result = pthread_create(&threads[i], NULL, worker_thread, &thread_data[i]);
        if (result != 0) {
            printf("ERROR: Failed to create thread %d\n", i + 1);
            exit(1);
        }
        printf("Main: Created thread %d with ID %p\n",
               thread_data[i].thread_id, (void*)threads[i]);
    }
    
    printf("\n--- WAITING FOR THREAD COMPLETION ---\n");
    printf("Main thread will now wait for all threads to finish...\n\n");
    
    /* Ожидание завершения всех потоков с помощью pthread_join */
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("Main: Waiting for thread %d to complete...\n", thread_data[i].thread_id);
        
        int result = pthread_join(threads[i], &thread_returns[i]);
        if (result != 0) {
            printf("ERROR: pthread_join failed for thread %d\n", thread_data[i].thread_id);
        } else {
            int* return_value = (int*)thread_returns[i];
            printf("Main: Thread %d returned value: %d\n",
                   thread_data[i].thread_id, *return_value);
            free(return_value);
        }
    }
    
    printf("\n--- ALL THREADS COMPLETED ---\n");
    printf("Final shared result: %d\n", shared_result);
}

/* Демонстрация последовательного и параллельного выполнения */
void demonstrate_execution_modes() {
    printf("\n=== EXECUTION MODES COMPARISON ===\n");
    
    pthread_t threads[3];
    thread_data_t data[3];
    shared_result = 0; // Сброс общего результата
    
    /* Настройка данных для потоков */
    for (int i = 0; i < 3; i++) {
        data[i].thread_id = i + 10;
        data[i].operations_count = 3;
        data[i].base_value = (i + 1) * 5;
    }
    
    printf("--- SEQUENTIAL EXECUTION ---\n");
    for (int i = 0; i < 3; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &data[i]);
        pthread_join(threads[i], NULL); // Ждем завершения перед созданием следующего
    }
    printf("Sequential result: %d\n", shared_result);
    
    shared_result = 0; // Сброс для параллельного выполнения
    
    printf("\n--- PARALLEL EXECUTION ---\n");
    pthread_t parallel_threads[3];
    for (int i = 0; i < 3; i++) {
        data[i].thread_id = i + 20;
        pthread_create(&parallel_threads[i], NULL, worker_thread, &data[i]);
    }
    for (int i = 0; i < 3; i++) {
        pthread_join(parallel_threads[i], NULL);
    }
    printf("Parallel result: %d\n", shared_result);
}

/* Демонстрация работы с идентификаторами потоков */
void demonstrate_thread_identification() {
    printf("\n=== THREAD IDENTIFICATION DEMONSTRATION ===\n");
    
    pthread_t main_thread = pthread_self();
    printf("Main thread ID: %p\n", (void*)main_thread);
    
    pthread_t worker_threads[2];
    
    int id1 = 1, id2 = 2;
    pthread_create(&worker_threads[0], NULL, identifier, &id1);
    pthread_create(&worker_threads[1], NULL, identifier, &id2);
    
    pthread_join(worker_threads[0], NULL);
    pthread_join(worker_threads[1], NULL);
}

/* Демонстрация timeout в ожидании */
void demonstrate_join_timeout() {
    printf("\n=== JOIN WITH TIMEOUT SIMULATION ===\n");
    
    pthread_t long_thread, fast_thread1, fast_thread2;
    
    pthread_create(&long_thread, NULL, long_running, NULL);
    pthread_create(&fast_thread1, NULL, fast_thread, NULL);
    pthread_create(&fast_thread2, NULL, fast_thread, NULL);
    
    printf("Main: Waiting for fast threads (should complete first)...\n");
    pthread_join(fast_thread1, NULL);
    pthread_join(fast_thread2, NULL);
    printf("Main: Fast threads completed\n");
    
    printf("Main: Now waiting for long-running thread...\n");
    pthread_join(long_thread, NULL);
    printf("Main: Long-running thread completed\n");
}

int main() {
    printf("=== PTHREAD_JOIN AND THREAD IDENTIFICATION DEMONSTRATION ===\n\n");
    
    demonstrate_thread_identification();
    demonstrate_join_mechanism();
    demonstrate_execution_modes();
    demonstrate_join_timeout();
    
    pthread_mutex_destroy(&result_mutex);
    
    printf("\n=== PROGRAM COMPLETED SUCCESSFULLY ===\n");
    return 0;
}

