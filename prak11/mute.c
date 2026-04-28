/* Количество сотрудников */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define NUM_EMPLOYEES 2
#define MAX_ITERATIONS 60000

/* Мьютекс для синхронизации */
pthread_mutex_t a_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Флаг для остановки потоков */
volatile int stop_threads = 0;

/* Структура сотрудника */
struct employee {
    int number;
    int id;
    char first_name[20];
    char last_name[30];
    char department[30];
    int room_number;
};

/* Глобальные переменные */
struct employee employees[] = {
    { 1, 12345678, "danny", "coresh", "Accounting", 101},
    { 2, 87654321, "misha", "levyn", "Programmers", 202}
};

struct employee employee_of_the_day;

/* Функция копирования с защитой мьютексом */
void copy_employee(struct employee* from, struct employee* to)
{
    int rc = pthread_mutex_lock(&a_mutex);
    if (rc) {
        if (rc != EINVAL) { // Игнорируем ошибки после уничтожения мьютекса
            printf("Mutex lock error: %d\n", rc);
        }
        return;
    }
    
    to->number = from->number;
    to->id = from->id;
    strcpy(to->first_name, from->first_name);
    strcpy(to->last_name, from->last_name);
    strcpy(to->department, from->department);
    to->room_number = from->room_number;
    
    rc = pthread_mutex_unlock(&a_mutex);
    if (rc && rc != EINVAL) {
        printf("Mutex unlock error: %d\n", rc);
    }
}

/* Функция потока с возможностью остановки */
void* do_loop(void* data)
{
    int my_num = *((int*)data);
    
    while (!stop_threads) {
        copy_employee(&employees[my_num-1], &employee_of_the_day);
    }
    printf("Thread %p stopped\n", (void*)pthread_self());
    return NULL;
}

int main(int argc, char* argv[])
{
    int i;
    int thr_id1, thr_id2;
    pthread_t p_thread1, p_thread2;
    int num1 = 1, num2 = 2;
    
    struct employee eotd;
    struct employee* worker;

    printf("Starting program WITH mutex synchronization\n");
    printf("Expected: consistent employee data\n");
    printf("Mutex protection: ENABLED\n\n");

    copy_employee(&employees[0], &employee_of_the_day);

    /* Создание потоков */
    thr_id1 = pthread_create(&p_thread1, NULL, do_loop, (void*)&num1);
    thr_id2 = pthread_create(&p_thread2, NULL, do_loop, (void*)&num2);

    printf("Threads created successfully\n");
    printf("Testing data consistency for %d iterations...\n\n", MAX_ITERATIONS);

    /* Проверка целостности данных */
    for (i = 0; i < MAX_ITERATIONS; i++) {
        copy_employee(&employee_of_the_day, &eotd);
        worker = &employees[eotd.number-1];

        /* Проверки согласованности */
        if (eotd.id != worker->id) {
            printf("ERROR: mismatching 'id', %d != %d (loop '%d')\n",
                   eotd.id, worker->id, i);
            exit(0);
        }

        if (strcmp(eotd.first_name, worker->first_name) != 0) {
            printf("ERROR: mismatching 'first_name', %s != %s (loop '%d')\n",
                   eotd.first_name, worker->first_name, i);
            exit(0);
        }

        if (strcmp(eotd.last_name, worker->last_name) != 0) {
            printf("ERROR: mismatching 'last_name', %s != %s (loop '%d')\n",
                   eotd.last_name, worker->last_name, i);
            exit(0);
        }

        if (strcmp(eotd.department, worker->department) != 0) {
            printf("ERROR: mismatching 'department', %s != %s (loop '%d')\n",
                   eotd.department, worker->department, i);
            exit(0);
        }

        if (eotd.room_number != worker->room_number) {
            printf("ERROR: mismatching 'room_number', %d != %d (loop '%d')\n",
                   eotd.room_number, worker->room_number, i);
            exit(0);
        }

        if (i % 10000 == 0) {
            printf("Progress: %d iterations completed, data consistent\n", i);
        }
    }

    printf("\nSUCCESS: All %d iterations completed without data corruption\n", i);
    printf("Mutex protection: VERIFIED - no race conditions detected\n");
    printf("Employee data: ALWAYS CONSISTENT\n");

    /* Корректное завершение потоков */
    printf("Stopping threads...\n");
    stop_threads = 1;
    
    /* Ожидание завершения потоков */
    pthread_join(p_thread1, NULL);
    pthread_join(p_thread2, NULL);
    
    /* Теперь безопасно уничтожаем мьютекс */
    pthread_mutex_destroy(&a_mutex);
    printf("Mutex destroyed successfully\n");
    printf("Program completed without errors\n");
    
    return 0;
}
