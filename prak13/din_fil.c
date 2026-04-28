/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 03.12.25
ПРАКТИЧЕСКАЯ РАБОТА №13
ИМЯ ФАЙЛА: din_fil.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация решения проблемы deadlock в классической задаче об обедающих философах.
Реализация антидедлок-стратегии на основе иерархии ресурсов, где вилки нумеруются и
захватываются в строго определенном порядке (от меньшего номера к большему).
Исследование эффективности использования pthread_mutex_trylock() и условных переменных
для предотвращения взаимных блокировок в многопоточных системах.
--------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>

#define DINER_COUNT 5
#define SIMULATION_TIME 30
#define MAX_WAIT_ATTEMPTS 3

/*
--------------------------------------------------
СТРУКТУРА: DiningUtensil
НАЗНАЧЕНИЕ: Структура для представления столового прибора (вилки)
ПОЛЯ:
    utensil_lock - мьютекс для синхронизации доступа к прибору
    is_available - флаг доступности прибора
    last_user - идентификатор последнего пользователя прибора
    available_cond - условная переменная для сигнализации о доступности
--------------------------------------------------
*/
typedef struct {
    pthread_mutex_t utensil_lock;
    bool is_available;
    int last_user;
    pthread_cond_t available_cond;
} DiningUtensil;

/*
--------------------------------------------------
СТРУКТУРА: PhilosopherData
НАЗНАЧЕНИЕ: Структура для хранения данных о философе
ПОЛЯ:
    seat_number - номер места за столом (идентификатор философа)
    meals_consumed - количество успешных трапез
    failed_attempts - количество неудачных попыток приема пищи
    left_utensil - указатель на левую вилку
    right_utensil - указатель на правую вилку
    continue_dining - флаг продолжения работы потока
    state_lock - мьютекс для защиты состояния философа
--------------------------------------------------
*/
typedef struct {
    int seat_number;
    int meals_consumed;
    int failed_attempts;
    DiningUtensil* left_utensil;
    DiningUtensil* right_utensil;
    bool continue_dining;
    pthread_mutex_t state_lock;
} PhilosopherData;

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: random_delay
НАЗНАЧЕНИЕ: Генерация случайной задержки с заданными границами
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    min_ms - минимальная задержка в миллисекундах
    max_ms - максимальная задержка в миллисекундах
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    nanosleep() - выполнение точной задержки
    rand() - генерация случайного значения
--------------------------------------------------
*/
static inline void random_delay(int min_ms, int max_ms) {
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = (min_ms + (rand() % (max_ms - min_ms + 1))) * 1000000L;
    nanosleep(&delay, NULL);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: try_acquire_utensil
НАЗНАЧЕНИЕ: Попытка захвата прибора с ограниченным числом попыток
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    utensil - указатель на захватываемый прибор
    philosopher_id - идентификатор философа
    utensil_id - идентификатор прибора
    max_attempts - максимальное количество попыток
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    true - прибор успешно захвачен, false - захват не удался
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_trylock() - неблокирующая попытка захвата мьютекса
    nanosleep() - пауза между попытками
    printf() - вывод информации о попытках захвата
--------------------------------------------------
*/
bool try_acquire_utensil(DiningUtensil* utensil, int philosopher_id,
                        int utensil_id, int max_attempts) {
    int attempts = 0;
    
    while (attempts < max_attempts) {
        /* Попытка немедленного захвата */
        if (pthread_mutex_trylock(&utensil->utensil_lock) == 0) {
            utensil->last_user = philosopher_id;
            printf("[Философ %d] Получил прибор %d (попытка %d)\n",
                   philosopher_id, utensil_id, attempts + 1);
            return true;
        }
        
        attempts++;
        if (attempts < max_attempts) {
            printf("[Философ %d] Ждет прибор %d... (попытка %d/%d)\n",
                   philosopher_id, utensil_id, attempts, max_attempts);
            
            /* Короткая пауза перед следующей попыткой */
            struct timespec short_wait;
            short_wait.tv_sec = 0;
            short_wait.tv_nsec = (100 + attempts * 50) * 1000000L; /* 100-250ms */
            nanosleep(&short_wait, NULL);
        }
    }
    
    printf("[Философ %d] Не удалось получить прибор %d после %d попыток\n",
           philosopher_id, utensil_id, max_attempts);
    return false;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: acquire_utensils_hierarchical
НАЗНАЧЕНИЕ: Антидедлок-стратегия на основе иерархии ресурсов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    data - указатель на данные философа
    first - указатель на первый прибор
    second - указатель на второй прибор
    first_id - идентификатор первого прибора
    second_id - идентификатор второго прибора
ВОЗВРАЩАЕМОЕ ЗНАЧЕНИЕ:
    true - оба прибора успешно захвачены, false - захват не удался
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    try_acquire_utensil() - попытка захвата отдельного прибора
    pthread_mutex_unlock() - освобождение частично захваченных ресурсов
--------------------------------------------------
*/
bool acquire_utensils_hierarchical(PhilosopherData* data,
                                  DiningUtensil* first, DiningUtensil* second,
                                  int first_id, int second_id) {
    /* Всегда захватываем прибор с меньшим номером сначала */
    if (first_id < second_id) {
        if (!try_acquire_utensil(first, data->seat_number, first_id, 2)) {
            return false;
        }
        if (!try_acquire_utensil(second, data->seat_number, second_id, 3)) {
            pthread_mutex_unlock(&first->utensil_lock);
            return false;
        }
    } else {
        if (!try_acquire_utensil(second, data->seat_number, second_id, 2)) {
            return false;
        }
        if (!try_acquire_utensil(first, data->seat_number, first_id, 3)) {
            pthread_mutex_unlock(&second->utensil_lock);
            return false;
        }
    }
    return true;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: contemplate
НАЗНАЧЕНИЕ: Имитация процесса размышления философа
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    philosopher_id - идентификатор философа
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    random_delay() - генерация случайной задержки для размышлений
    printf() - вывод информации о состоянии философа
--------------------------------------------------
*/
void contemplate(int philosopher_id) {
    printf("[Философ %d] Размышляет о бытии\n", philosopher_id);
    random_delay(800, 1500);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: consume_meal
НАЗНАЧЕНИЕ: Имитация процесса приема пищи философом
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    philosopher_id - идентификатор философа
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    random_delay() - генерация случайной задержки для приема пищи
    printf() - вывод информации о состоянии философа
--------------------------------------------------
*/
void consume_meal(int philosopher_id) {
    printf("[Философ %d] Наслаждается изысканным блюдом\n", philosopher_id);
    random_delay(600, 1200);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: philosopher_routine
НАЗНАЧЕНИЕ: Основная функция потока философа с антидедлок-стратегией
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    param - указатель на структуру PhilosopherData
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    contemplate() - процесс размышления
    consume_meal() - процесс приема пищи
    pthread_mutex_lock() - захват мьютекса для защиты состояния
    pthread_mutex_unlock() - освобождение мьютекса
    acquire_utensils_hierarchical() - захват приборов по иерархии
    random_delay() - паузы между попытками
    printf() - вывод информации о работе философа
--------------------------------------------------
*/
void* philosopher_routine(void* param) {
    PhilosopherData* data = (PhilosopherData*)param;
    
    while (data->continue_dining) {
        contemplate(data->seat_number);
        
        pthread_mutex_lock(&data->state_lock);
        data->failed_attempts = 0;
        pthread_mutex_unlock(&data->state_lock);
        
        bool dining_successful = false;
        int consecutive_failures = 0;
        
        while (!dining_successful && data->continue_dining) {
            printf("[Философ %d] Испытывает интеллектуальный голод\n", data->seat_number);
            
            /* Определяем, какие приборы будем захватывать */
            int left_id = data->seat_number;
            int right_id = (data->seat_number + 1) % DINER_COUNT;
            
            /* Используем стратегию иерархии ресурсов */
            dining_successful = acquire_utensils_hierarchical(data,
                data->left_utensil, data->right_utensil, left_id, right_id);
            
            if (dining_successful) {
                /* Успешный захват обоих приборов */
                consume_meal(data->seat_number);
                
                pthread_mutex_lock(&data->state_lock);
                data->meals_consumed++;
                data->failed_attempts = 0;
                pthread_mutex_unlock(&data->state_lock);
                
                printf("[Философ %d] Завершил трапезу №%d\n",
                       data->seat_number, data->meals_consumed);
                
                /* Освобождение приборов (в обратном порядке не требуется
                   из-за иерархической стратегии) */
                pthread_mutex_unlock(&data->right_utensil->utensil_lock);
                pthread_mutex_unlock(&data->left_utensil->utensil_lock);
                
                consecutive_failures = 0;
                
            } else {
                /* Неудачная попытка */
                pthread_mutex_lock(&data->state_lock);
                data->failed_attempts++;
                consecutive_failures++;
                pthread_mutex_unlock(&data->state_lock);
                
                if (consecutive_failures >= MAX_WAIT_ATTEMPTS) {
                    printf("[Философ %d] Делает медитативную паузу после %d неудач\n",
                           data->seat_number, consecutive_failures);
                    random_delay(1500, 2500);
                    consecutive_failures = 0;
                    
                    pthread_mutex_lock(&data->state_lock);
                    data->failed_attempts = 0;
                    pthread_mutex_unlock(&data->state_lock);
                } else {
                    /* Короткая пауза перед следующей попыткой */
                    random_delay(300, 600);
                }
            }
        }
    }
    
    printf("[Философ %d] Завершает ужин. Всего трапез: %d\n",
           data->seat_number, data->meals_consumed);
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: init_dining_system
НАЗНАЧЕНИЕ: Инициализация системы обедающих философов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    utensils - массив приборов (вилок)
    philosophers - массив данных философов
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_init() - инициализация мьютексов приборов
    pthread_cond_init() - инициализация условных переменных
    pthread_mutex_init() - инициализация мьютексов состояния философов
--------------------------------------------------
*/
void init_dining_system(DiningUtensil utensils[], PhilosopherData philosophers[]) {
    for (int i = 0; i < DINER_COUNT; i++) {
        pthread_mutex_init(&utensils[i].utensil_lock, NULL);
        pthread_cond_init(&utensils[i].available_cond, NULL);
        utensils[i].is_available = true;
        utensils[i].last_user = -1;
    }
    
    for (int i = 0; i < DINER_COUNT; i++) {
        philosophers[i].seat_number = i;
        philosophers[i].meals_consumed = 0;
        philosophers[i].failed_attempts = 0;
        philosophers[i].left_utensil = &utensils[i];
        philosophers[i].right_utensil = &utensils[(i + 1) % DINER_COUNT];
        philosophers[i].continue_dining = true;
        pthread_mutex_init(&philosophers[i].state_lock, NULL);
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: cleanup_system
НАЗНАЧЕНИЕ: Освобождение ресурсов системы обедающих философов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    utensils - массив приборов для очистки
    philosophers - массив данных философов для очистки
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_destroy() - уничтожение мьютексов приборов
    pthread_cond_destroy() - уничтожение условных переменных
    pthread_mutex_destroy() - уничтожение мьютексов состояния философов
--------------------------------------------------
*/
void cleanup_system(DiningUtensil utensils[], PhilosopherData philosophers[]) {
    for (int i = 0; i < DINER_COUNT; i++) {
        pthread_mutex_destroy(&utensils[i].utensil_lock);
        pthread_cond_destroy(&utensils[i].available_cond);
        pthread_mutex_destroy(&philosophers[i].state_lock);
    }
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: generate_performance_report
НАЗНАЧЕНИЕ: Генерация отчета о работе системы обедающих философов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    philosophers - массив данных философов для анализа
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    printf() - форматированный вывод статистики работы системы
--------------------------------------------------
*/
void generate_performance_report(PhilosopherData philosophers[]) {
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   ОТЧЕТ РЕСТОРАНА 'ФИЛОСОФСКИЕ ТРАПЕЗЫ'  ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("Время работы: %d секунд\n", SIMULATION_TIME);
    printf("Количество философов: %d\n\n", DINER_COUNT);
    
    int total_meals = 0;
    printf("Статистика по философам:\n");
    printf("┌──────┬─────────────┐\n");
    printf("│  ID  │  Трапезы    │\n");
    printf("├──────┼─────────────┤\n");
    
    for (int i = 0; i < DINER_COUNT; i++) {
        printf("│  %2d  │    %4d     │\n",
               philosophers[i].seat_number, philosophers[i].meals_consumed);
        total_meals += philosophers[i].meals_consumed;
    }
    
    printf("└──────┴─────────────┘\n");
    
    printf("\nИтоговая статистика:\n");
    printf("  Всего трапез: %d\n", total_meals);
    printf("  Среднее на философа: %.2f\n", (float)total_meals / DINER_COUNT);
    printf("  Производительность: %.2f трапез/секунду\n",
           (float)total_meals / SIMULATION_TIME);
    
    /* Определение самого голодного философа */
    int max_meals = 0;
    int hungry_philosopher = 0;
    for (int i = 0; i < DINER_COUNT; i++) {
        if (philosophers[i].meals_consumed > max_meals) {
            max_meals = philosophers[i].meals_consumed;
            hungry_philosopher = i;
        }
    }
    printf("  Самый прожорливый: философ %d (%d трапез)\n",
           hungry_philosopher, max_meals);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, демонстрирующая решение задачи об обедающих философах
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание потоков философов
    pthread_join() - ожидание завершения потоков философов
    init_dining_system() - инициализация системы
    cleanup_system() - очистка ресурсов системы
    generate_performance_report() - генерация отчета
    srand() - инициализация генератора случайных чисел
    time() - получение текущего времени
    sleep() - задание времени работы симуляции
    printf() - вывод информации о системе и стратегии предотвращения дедлоков
    perror() - обработка ошибок создания потоков
--------------------------------------------------
*/
int main(void) {
    pthread_t philosopher_threads[DINER_COUNT];
    DiningUtensil dining_utensils[DINER_COUNT];
    PhilosopherData philosophers_data[DINER_COUNT];
    
    srand(time(NULL));
    
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   РЕСТОРАН 'ФИЛОСОФСКИЕ ТРАПЕЗЫ'         ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("\nКонфигурация системы:\n");
    printf("  • Количество философов: %d\n", DINER_COUNT);
    printf("  • Время работы: %d секунд\n", SIMULATION_TIME);
    printf("  • Максимум попыток подряд: %d\n\n", MAX_WAIT_ATTEMPTS);
    
    printf("Анти-дедлок стратегии:\n");
    printf("  1. Иерархия ресурсов - всегда захватываем прибор\n");
    printf("     с меньшим номером сначала\n");
    printf("  2. pthread_mutex_trylock() вместо блокирующего ожидания\n");
    printf("  3. Экспоненциальные паузы между попытками\n");
    printf("  4. Принудительные перерывы после нескольких неудач\n\n");
    
    init_dining_system(dining_utensils, philosophers_data);
    
    /* Создание потоков философов */
    for (int i = 0; i < DINER_COUNT; i++) {
        if (pthread_create(&philosopher_threads[i], NULL,
                          philosopher_routine, &philosophers_data[i]) != 0) {
            perror("Ошибка создания потока философа");
            return EXIT_FAILURE;
        }
    }
    
    printf("Начало философских трапез...\n");
    printf("==========================================\n");
    
    /* Основное время работы */
    sleep(SIMULATION_TIME);
    
    /* Корректное завершение потоков */
    printf("\n==========================================\n");
    printf("Завершение работы ресторана...\n");
    
    for (int i = 0; i < DINER_COUNT; i++) {
        philosophers_data[i].continue_dining = false;
    }
    
    /* Даем время философам закончить текущие действия */
    sleep(2);
    
    for (int i = 0; i < DINER_COUNT; i++) {
        pthread_join(philosopher_threads[i], NULL);
    }
    
    generate_performance_report(philosophers_data);
    cleanup_system(dining_utensils, philosophers_data);
    
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   РЕСТОРАН ЗАКРЫТ. ДО НОВЫХ ВСТРЕЧ!      ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    
    return EXIT_SUCCESS;
}
