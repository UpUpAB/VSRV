/*
--------------------------------------------------
ИМЯ: Мезенцев Егор Александрович ID пользователя: 23К0163
СРОК: 08.12.25
ПРАКТИЧЕСКАЯ РАБОТА №15
ИМЯ ФАЙЛА: prod.c
НАЗНАЧЕНИЕ ПРОГРАММЫ:
Демонстрация модели конвейерного производства с использованием мьютексов и условных
переменных для синхронизации потоков. Реализация производственной линии с тремя
независимыми потоками для производства компонентов A, B, C, потоком сборки модулей
(A+B) и потоком финальной сборки изделий (модуль+C). Исследование механизмов
координации параллельных процессов в производственной системе с буферизацией
промежуточных продуктов.
--------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>


#define PRODUCTION_CYCLES 3
#define MAX_COMPONENTS 10

/*
--------------------------------------------------
СТРУКТУРА: ProductionParams
НАЗНАЧЕНИЕ: Структура для передачи параметров в производственные потоки
ПОЛЯ:
    component_id - идентификатор компонента (A, B или C)
    production_time - время производства компонента в секундах
    produced_count - указатель на счетчик произведенных компонентов
    counter_lock - указатель на мьютекс для защиты счетчика
    assembly_line - указатель на сборочную линию
--------------------------------------------------
*/
typedef struct {
    char component_id;
    int production_time;
    int* produced_count;
    pthread_mutex_t* counter_lock;
    void* assembly_line;
} ProductionParams;

/*
--------------------------------------------------
СТРУКТУРА: ComponentBuffer
НАЗНАЧЕНИЕ: Структура для представления буфера компонентов
ПОЛЯ:
    available_count - количество доступных компонентов в буфере
    access_lock - мьютекс для синхронизации доступа к буферу
    available_cond - условная переменная для сигнализации о наличии компонентов
--------------------------------------------------
*/
typedef struct {
    int available_count;
    pthread_mutex_t access_lock;
    pthread_cond_t available_cond;
} ComponentBuffer;

/*
--------------------------------------------------
СТРУКТУРА: AssemblyLine
НАЗНАЧЕНИЕ: Структура для представления сборочной линии
ПОЛЯ:
    product_id - идентификатор текущего изделия
    component_a_buffer - указатель на буфер компонента A
    component_b_buffer - указатель на буфер компонента B
    component_c_buffer - указатель на буфер компонента C
    module_buffer - указатель на буфер собранных модулей
    assembly_lock - мьютекс для синхронизации сборки
--------------------------------------------------
*/
typedef struct {
    int product_id;
    ComponentBuffer* component_a_buffer;
    ComponentBuffer* component_b_buffer;
    ComponentBuffer* component_c_buffer;
    ComponentBuffer* module_buffer;
    pthread_mutex_t assembly_lock;
} AssemblyLine;

/* Глобальный счетчик завершенных изделий */
static int total_products_completed = 0;
static pthread_mutex_t completion_counter_lock = PTHREAD_MUTEX_INITIALIZER;

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: init_component_buffer
НАЗНАЧЕНИЕ: Инициализация буфера компонентов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    buffer - указатель на буфер для инициализации
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_init() - инициализация мьютекса доступа
    pthread_cond_init() - инициализация условной переменной
--------------------------------------------------
*/
void init_component_buffer(ComponentBuffer* buffer) {
    buffer->available_count = 0;
    pthread_mutex_init(&buffer->access_lock, NULL);
    pthread_cond_init(&buffer->available_cond, NULL);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: cleanup_component_buffer
НАЗНАЧЕНИЕ: Освобождение ресурсов буфера компонентов
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    buffer - указатель на буфер для очистки
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_destroy() - уничтожение мьютекса доступа
    pthread_cond_destroy() - уничтожение условной переменной
--------------------------------------------------
*/
void cleanup_component_buffer(ComponentBuffer* buffer) {
    pthread_mutex_destroy(&buffer->access_lock);
    pthread_cond_destroy(&buffer->available_cond);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: add_to_buffer
НАЗНАЧЕНИЕ: Добавление компонента в буфер с синхронизацией
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    buffer - указатель на буфер для добавления компонента
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к буферу
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_signal() - оповещение ожидающих потоков о новом компоненте
    printf() - вывод информации о состоянии буфера
--------------------------------------------------
*/
void add_to_buffer(ComponentBuffer* buffer) {
    pthread_mutex_lock(&buffer->access_lock);
    buffer->available_count++;
    printf("[Буфер] Добавлен компонент. Теперь доступно: %d\n", buffer->available_count);
    pthread_cond_signal(&buffer->available_cond);
    pthread_mutex_unlock(&buffer->access_lock);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: get_from_buffer
НАЗНАЧЕНИЕ: Получение компонента из буфера с ожиданием при необходимости
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    buffer - указатель на буфер для получения компонента
    consumer_name - имя потребителя для логирования
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_mutex_lock() - захват мьютекса для доступа к буферу
    pthread_mutex_unlock() - освобождение мьютекса
    pthread_cond_wait() - ожидание появления компонентов в пустом буфере
    printf() - вывод информации о получении компонента
--------------------------------------------------
*/
void get_from_buffer(ComponentBuffer* buffer, const char* consumer_name) {
    pthread_mutex_lock(&buffer->access_lock);
    
    printf("[%s] Проверка буфера... доступно: %d\n",
           consumer_name, buffer->available_count);
    
    while (buffer->available_count == 0) {
        printf("[%s] Ожидание компонента...\n", consumer_name);
        pthread_cond_wait(&buffer->available_cond, &buffer->access_lock);
    }
    
    buffer->available_count--;
    printf("[%s] Получен компонент. Осталось: %d\n",
           consumer_name, buffer->available_count);
    
    pthread_mutex_unlock(&buffer->access_lock);
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: simulate_production_process
НАЗНАЧЕНИЕ: Имитация производственного процесса с визуализацией прогресса
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    component_name - название производственного процесса
    duration_seconds - длительность процесса в секундах
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    printf() - вывод информации о ходе процесса
    fflush() - принудительный вывод буфера
    sleep() - имитация времени выполнения процесса
--------------------------------------------------
*/
void simulate_production_process(const char* component_name, int duration_seconds) {
    printf("[%-15s] Производство", component_name);
    fflush(stdout);
    
    for (int i = 0; i < duration_seconds; i++) {
        printf(".");
        fflush(stdout);
        sleep(1);
    }
    
    printf(" Завершено!\n");
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: produce_component
НАЗНАЧЕНИЕ: Функция потока производства компонента
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    arg - указатель на структуру ProductionParams с параметрами производства
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    simulate_production_process() - имитация процесса производства
    pthread_mutex_lock() - захват мьютекса для защиты счетчика
    pthread_mutex_unlock() - освобождение мьютекса
    add_to_buffer() - добавление произведенного компонента в буфер
    printf() - вывод информации о производстве компонента
--------------------------------------------------
*/
void* produce_component(void* arg) {
    ProductionParams* params = (ProductionParams*)arg;
    AssemblyLine* line = (AssemblyLine*)params->assembly_line;
    ComponentBuffer* target_buffer = NULL;
    
    /* Определяем целевой буфер для компонента */
    if (params->component_id == 'A') {
        target_buffer = line->component_a_buffer;
    } else if (params->component_id == 'B') {
        target_buffer = line->component_b_buffer;
    } else if (params->component_id == 'C') {
        target_buffer = line->component_c_buffer;
    }
    
    for (int cycle = 0; cycle < PRODUCTION_CYCLES; cycle++) {
        printf("\n[ЦИКЛ %d] Производство компонента %c\n",
               cycle + 1, params->component_id);
        
        simulate_production_process("Компонент", params->production_time);
        
        pthread_mutex_lock(params->counter_lock);
        (*params->produced_count)++;
        int current_count = *params->produced_count;
        pthread_mutex_unlock(params->counter_lock);
        
        printf("[Компонент %c] Произведен (всего: %d)\n",
               params->component_id, current_count);
        
        /* Помещаем компонент в соответствующий буфер */
        if (target_buffer) {
            add_to_buffer(target_buffer);
        }
    }
    
    printf("\n[Компонент %c] Производство завершено\n", params->component_id);
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: assemble_module
НАЗНАЧЕНИЕ: Функция потока сборки модулей из компонентов A и B
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    arg - указатель на структуру AssemblyLine с данными сборочной линии
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    get_from_buffer() - получение компонентов A и B из буферов
    simulate_production_process() - имитация процесса сборки модуля
    add_to_buffer() - добавление собранного модуля в буфер
    printf() - вывод информации о сборке модуля
--------------------------------------------------
*/
void* assemble_module(void* arg) {
    AssemblyLine* line = (AssemblyLine*)arg;
    
    for (int module_num = 0; module_num < PRODUCTION_CYCLES; module_num++) {
        printf("\n[МОДУЛЬ %d] Ожидание компонентов A и B...\n", module_num + 1);
        
        get_from_buffer(line->component_a_buffer, "Модуль-сборка");
        get_from_buffer(line->component_b_buffer, "Модуль-сборка");
        
        printf("[МОДУЛЬ %d] Компоненты получены. Сборка...\n", module_num + 1);
        
        /* Имитация времени сборки модуля */
        simulate_production_process("Сборка модуля", 1);
        
        printf("[МОДУЛЬ %d] Сборка завершена\n", module_num + 1);
        
        /* Помещаем собранный модуль в буфер */
        add_to_buffer(line->module_buffer);
    }
    
    printf("\n[СБОРКА МОДУЛЕЙ] Все модули собраны\n");
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: final_assembly
НАЗНАЧЕНИЕ: Функция потока финальной сборки изделий (модуль + компонент C)
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    arg - указатель на структуру AssemblyLine с данными сборочной линии
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    get_from_buffer() - получение модуля и компонента C из буферов
    simulate_production_process() - имитация процесса финальной сборки
    pthread_mutex_lock() - захват мьютекса для защиты глобального счетчика
    pthread_mutex_unlock() - освобождение мьютекса
    printf() - вывод информации о финальной сборке
--------------------------------------------------
*/
void* final_assembly(void* arg) {
    AssemblyLine* line = (AssemblyLine*)arg;
    
    for (int product_num = 0; product_num < PRODUCTION_CYCLES; product_num++) {
        line->product_id = product_num + 1;
        
        printf("\n═══════════════════════════════════════════════\n");
        printf("[ИЗДЕЛИЕ %d] Ожидание модуля и компонента C...\n", line->product_id);
        
        /* Ожидание готовности модуля и компонента C */
        get_from_buffer(line->module_buffer, "Финальная сборка");
        get_from_buffer(line->component_c_buffer, "Финальная сборка");
        
        printf("[ИЗДЕЛИЕ %d] Все компоненты получены\n", line->product_id);
        printf("[ИЗДЕЛИЕ %d] Начало финальной сборки...\n", line->product_id);
        
        /* Имитация времени финальной сборки */
        simulate_production_process("Финальная сборка", 2);
        
        pthread_mutex_lock(&completion_counter_lock);
        total_products_completed++;
        int current_total = total_products_completed;
        pthread_mutex_unlock(&completion_counter_lock);
        
        printf("[ИЗДЕЛИЕ %d] ГОТОВО! (Всего собрано: %d)\n",
               line->product_id, current_total);
        printf("═══════════════════════════════════════════════\n");
    }
    
    return NULL;
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: generate_production_report
НАЗНАЧЕНИЕ: Генерация отчета о работе производственной линии
ИСПОЛЬЗОВАНИЕ ПАРАМЕТРОВ:
    component_a_count - количество произведенных компонентов A
    component_b_count - количество произведенных компонентов B
    component_c_count - количество произведенных компонентов C
    line - указатель на сборочную линию для получения данных о буферах
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    printf() - форматированный вывод статистики производства
--------------------------------------------------
*/
void generate_production_report(int component_a_count, int component_b_count,
                               int component_c_count, AssemblyLine* line) {
    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║         ОТЧЕТ О ПРОИЗВОДСТВЕ               ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");
    
    printf("Конфигурация производственной линии:\n");
    printf("  • Количество циклов производства: %d\n", PRODUCTION_CYCLES);
    printf("  • Максимальная емкость буферов: %d\n\n", MAX_COMPONENTS);
    
    printf("Произведено компонентов:\n");
    printf("  • Компонент A: %d шт. (время: 1 сек.)\n", component_a_count);
    printf("  • Компонент B: %d шт. (время: 2 сек.)\n", component_b_count);
    printf("  • Компонент C: %d шт. (время: 3 сек.)\n", component_c_count);
    
    int total_components = component_a_count + component_b_count + component_c_count;
    
    printf("\nОстатки в буферах:\n");
    printf("  • Компонент A: %d\n", line->component_a_buffer->available_count);
    printf("  • Компонент B: %d\n", line->component_b_buffer->available_count);
    printf("  • Компонент C: %d\n", line->component_c_buffer->available_count);
    printf("  • Модули: %d\n", line->module_buffer->available_count);
    
    printf("\nИтог производства:\n");
    printf("  • Всего компонентов: %d\n", total_components);
    printf("  • Всего изделий собрано: %d\n", total_products_completed);
    printf("  • Эффективность линии: %.1f%%\n",
           (float)total_products_completed / PRODUCTION_CYCLES * 100);
    
    printf("\n══════════════════════════════════════════════\n");
}

/*
--------------------------------------------------
НАЗНАЧЕНИЕ ФУНКЦИИ: main
НАЗНАЧЕНИЕ: Главная функция программы, координирующая работу производственной линии
ВЫЗЫВАЕМЫЕ ФУНКЦИИ:
    pthread_create() - создание потоков производства компонентов и сборки
    pthread_join() - ожидание завершения всех потоков
    init_component_buffer() - инициализация буферов компонентов
    cleanup_component_buffer() - очистка ресурсов буферов
    pthread_mutex_init() - инициализация мьютексов счетчиков
    pthread_mutex_destroy() - уничтожение мьютексов
    generate_production_report() - генерация итогового отчета
    perror() - обработка ошибок создания потоков
    printf() - вывод информации о конфигурации и работе системы
--------------------------------------------------
*/
int main(void) {
    pthread_t production_threads[3];
    pthread_t module_assembly_thread;
    pthread_t final_assembly_thread;
    
    /* Счетчики произведенных компонентов */
    int component_a_count = 0;
    int component_b_count = 0;
    int component_c_count = 0;
    
    pthread_mutex_t counter_a_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t counter_b_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t counter_c_lock = PTHREAD_MUTEX_INITIALIZER;
    
    /* Инициализация буферов */
    ComponentBuffer buffer_a, buffer_b, buffer_c, module_buffer;
    init_component_buffer(&buffer_a);
    init_component_buffer(&buffer_b);
    init_component_buffer(&buffer_c);
    init_component_buffer(&module_buffer);
    
    /* Инициализация сборочной линии */
    AssemblyLine assembly_line = {
        .product_id = 0,
        .component_a_buffer = &buffer_a,
        .component_b_buffer = &buffer_b,
        .component_c_buffer = &buffer_c,
        .module_buffer = &module_buffer,
        .assembly_lock = PTHREAD_MUTEX_INITIALIZER
    };
    
    /* Параметры для потоков производства */
    ProductionParams params_a = {
        .component_id = 'A',
        .production_time = 1,
        .produced_count = &component_a_count,
        .counter_lock = &counter_a_lock,
        .assembly_line = &assembly_line
    };
    
    ProductionParams params_b = {
        .component_id = 'B',
        .production_time = 2,
        .produced_count = &component_b_count,
        .counter_lock = &counter_b_lock,
        .assembly_line = &assembly_line
    };
    
    ProductionParams params_c = {
        .component_id = 'C',
        .production_time = 3,
        .produced_count = &component_c_count,
        .counter_lock = &counter_c_lock,
        .assembly_line = &assembly_line
    };
    
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   ЗАПУСК ПРОИЗВОДСТВЕННОЙ ЛИНИИ            ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");
    
    printf("Конфигурация производственной линии:\n");
    printf("  • Количество производственных циклов: %d\n", PRODUCTION_CYCLES);
    printf("  • Время производства компонента A: %d сек.\n", params_a.production_time);
    printf("  • Время производства компонента B: %d сек.\n", params_b.production_time);
    printf("  • Время производства компонента C: %d сек.\n", params_c.production_time);
    printf("  • Время сборки модуля: 1 сек.\n");
    printf("  • Время финальной сборки: 2 сек.\n\n");
    
    printf("Механизм синхронизации:\n");
    printf("  • Используются мьютексы и условные переменные\n");
    printf("  • Буферизация компонентов между этапами\n");
    printf("  • Контроль доступа к общим ресурсам\n\n");
    
    printf("Запуск производственных потоков...\n");
    printf("══════════════════════════════════════════════\n\n");
    
    /* Создание потоков производства компонентов */
    if (pthread_create(&production_threads[0], NULL, produce_component, &params_a) != 0 ||
        pthread_create(&production_threads[1], NULL, produce_component, &params_b) != 0 ||
        pthread_create(&production_threads[2], NULL, produce_component, &params_c) != 0) {
        perror("Ошибка создания производственных потоков");
        return EXIT_FAILURE;
    }
    
    /* Создание потока сборки модулей */
    if (pthread_create(&module_assembly_thread, NULL, assemble_module, &assembly_line) != 0) {
        perror("Ошибка создания потока сборки модулей");
        return EXIT_FAILURE;
    }
    
    /* Создание потока финальной сборки */
    if (pthread_create(&final_assembly_thread, NULL, final_assembly, &assembly_line) != 0) {
        perror("Ошибка создания потока финальной сборки");
        return EXIT_FAILURE;
    }
    
    /* Ожидание завершения всех потоков */
    for (int i = 0; i < 3; i++) {
        pthread_join(production_threads[i], NULL);
    }
    
    pthread_join(module_assembly_thread, NULL);
    pthread_join(final_assembly_thread, NULL);
    
    printf("\n══════════════════════════════════════════════\n");
    printf("Производственная линия завершила работу\n");
    
    generate_production_report(component_a_count, component_b_count,
                             component_c_count, &assembly_line);
    
    /* Очистка ресурсов */
    cleanup_component_buffer(&buffer_a);
    cleanup_component_buffer(&buffer_b);
    cleanup_component_buffer(&buffer_c);
    cleanup_component_buffer(&module_buffer);
    
    pthread_mutex_destroy(&counter_a_lock);
    pthread_mutex_destroy(&counter_b_lock);
    pthread_mutex_destroy(&counter_c_lock);
    pthread_mutex_destroy(&assembly_line.assembly_lock);
    pthread_mutex_destroy(&completion_counter_lock);
    
    printf("Программа успешно завершена.\n");
    return EXIT_SUCCESS;
}
