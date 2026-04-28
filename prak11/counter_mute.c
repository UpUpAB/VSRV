#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#define NUM_THREADS 5
#define ITERATIONS 10000

int global_counter = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Безопасное увеличение счетчика */
void increment_counter_safe(int thread_id)
{
    pthread_mutex_lock(&counter_mutex);
    
    int old_value = global_counter;
    for (int i = 0; i < 100; i++) {} // короткая задержка
    global_counter = old_value + 1;
    
    /* Выводим только каждое 1000-е значение */
    if (global_counter % 1000 == 0) {
        printf("Thread %d: counter = %d\n", thread_id, global_counter);
    }
    
    pthread_mutex_unlock(&counter_mutex);
}

/* Небезопасное увеличение счетчика */
void increment_counter_unsafe(int thread_id)
{
    int old_value = global_counter;
    for (int i = 0; i < 100; i++) {}
    global_counter = old_value + 1;
    
    if (global_counter % 1000 == 0) {
        printf("Thread %d: counter = %d\n", thread_id, global_counter);
    }
}

void* thread_function_safe(void* arg)
{
    int thread_id = *((int*)arg);
    for (int i = 0; i < ITERATIONS; i++) {
        increment_counter_safe(thread_id);
    }
    printf("Thread %d completed safe iterations\n", thread_id);
    return NULL;
}

void* thread_function_unsafe(void* arg)
{
    int thread_id = *((int*)arg);
    for (int i = 0; i < ITERATIONS; i++) {
        increment_counter_unsafe(thread_id);
    }
    printf("Thread %d completed unsafe iterations\n", thread_id);
    return NULL;
}

void test_safe_counter()
{
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    printf("\n=== SAFE COUNTER WITH MUTEX ===\n");
    printf("Threads: %d, Iterations: %d, Expected: %d\n",
           NUM_THREADS, ITERATIONS, NUM_THREADS * ITERATIONS);
    
    global_counter = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i + 1;
        pthread_create(&threads[i], NULL, thread_function_safe, &thread_ids[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final value: %d (Expected: %d)\n",
           global_counter, NUM_THREADS * ITERATIONS);
    
    if (global_counter == NUM_THREADS * ITERATIONS) {
        printf("RESULT: SUCCESS - No data loss\n");
    } else {
        printf("RESULT: FAILED - Lost %d increments\n",
               (NUM_THREADS * ITERATIONS) - global_counter);
    }
}

void test_unsafe_counter()
{
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    printf("\n=== UNSAFE COUNTER WITHOUT MUTEX ===\n");
    printf("Threads: %d, Iterations: %d, Expected: %d\n",
           NUM_THREADS, ITERATIONS, NUM_THREADS * ITERATIONS);
    
    global_counter = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i + 1;
        pthread_create(&threads[i], NULL, thread_function_unsafe, &thread_ids[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Final value: %d (Expected: %d)\n",
           global_counter, NUM_THREADS * ITERATIONS);
    
    if (global_counter == NUM_THREADS * ITERATIONS) {
        printf("RESULT: LUCKY - No data loss (but race conditions possible)\n");
    } else {
        printf("RESULT: EXPECTED - Lost %d increments due to race conditions\n",
               (NUM_THREADS * ITERATIONS) - global_counter);
    }
}

void demonstrate_trylock()
{
    printf("\n=== TRYLOCK DEMONSTRATION ===\n");
    
    pthread_mutex_lock(&counter_mutex);
    printf("Mutex locked successfully\n");
    
    int rc = pthread_mutex_trylock(&counter_mutex);
    if (rc == EBUSY) {
        printf("Trylock failed - mutex busy (expected)\n");
    }
    
    pthread_mutex_unlock(&counter_mutex);
    printf("Mutex unlocked\n");
}

int main()
{
    printf("=== MUTEX COUNTER DEMONSTRATION ===\n");
    
    demonstrate_trylock();
    test_safe_counter();
    test_unsafe_counter();
    
    pthread_mutex_destroy(&counter_mutex);
    printf("\nProgram completed\n");
    
    return 0;
}

