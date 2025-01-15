#include <stdio.h> // Отвечает за стандартный ввод/вывод fprintf()
#include <stdlib.h> // Стандартная библиотека общего назначения exit()
#include <unistd.h> // Содержит функции для работы с системным интерфейсом POSIX fork() chdir()
#include <sys/types.h> // Определяет типы данных, используемые в системных вызовах pid_t
#include <time.h> // Отвечает за функции работы с датой и временем
#include <string.h> // Содержит функции для работы со строками

// Функция для получения температуры CPU
float get_cpu_temperature() {
    FILE *temp_file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!temp_file) {
        return -1.0; // Ошибка чтения файла
    }
    int temp_milli;
    fscanf(temp_file, "%d", &temp_milli);
    fclose(temp_file);
    return temp_milli / 1000.0; // Преобразование в градусы Цельсия
}

int main() {
    pid_t pid, sid;

    // 1. Создаем новый процесс
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE); // Ошибка при создании
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Родительский процесс завершает работу
    }

    // 2. Создаем новый сеанс
    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    // 3. Изменяем текущий рабочий каталог
    if (chdir("/") < 0) {
        exit(EXIT_FAILURE);
    }

    // 4. Закрываем файловые дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // 5. Открываем лог-файл
    FILE *log_file = fopen("/var/log/mydaemon.log", "a");
    if (!log_file) {
        exit(EXIT_FAILURE);
    }

    // 6. Основной цикл демона
    while (1) {
        // Получаем текущее время
        time_t now = time(NULL);
        struct tm *local_time = localtime(&now);

        // Форматируем строку времени
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);

        // Получаем температуру процессора
        float cpu_temp = get_cpu_temperature();

        // Пишем данные в лог
        if (cpu_temp < 0) {
            fprintf(log_file, "[%s] Error reading CPU temperature\n", time_str);
        } else {
            fprintf(log_file, "[%s] CPU Temperature: %.2f°C\n", time_str, cpu_temp);
        }

        fflush(log_file); // Сбрасываем буфер, чтобы данные записались
        sleep(10);        // Ждем 10 секунд
    }

    fclose(log_file);
    exit(EXIT_SUCCESS);
}
