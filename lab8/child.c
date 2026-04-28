#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    // Вывод сообщения о запуске и PID процесса
    printf("Дочерний процесс: PID = %d запущен\n", getpid());

    // Имитация работы
    sleep(1);

    // Генерация случайного кода завершения 0-4
    int exit_code = rand() % 5;

    printf("Дочерний процесс: PID = %d завершается с кодом %d\n",
           getpid(), exit_code);

    exit(exit_code);
}
