#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/statvfs.h>

#define URL "http://192.168.1.180:8080/data"
#define TIME_FORMAT "%Y-%m-%d %H:%M:%S"
#define JSON_BUFFER_SIZE 512
#define HOSTNAME_BUFFER_SIZE 128
#define SLEEP_TIME 10

// Функция для получения имени хоста устройства
void get_hostname(char *buffer, size_t size) {
    if (gethostname(buffer, size) != 0) {
        perror("Failed to get hostname");
        strncpy(buffer, "unknown", size); // Устанавливаем значение по умолчанию
    }
}

// Функция для получения текущего времени в виде строки
void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    strftime(buffer, size, TIME_FORMAT, local_time);
}

// Функция для получения использования памяти на файловой системе
void get_mem_usage(const char *path, unsigned long *used, unsigned long *total) {
    struct statvfs stats;

    // Получаем статистику для указанного пути
    if (statvfs(path, &stats) != 0) {
        perror("Failed to get filesystem statistics");
        *used = 0;
        *total = 0;
        exit(EXIT_FAILURE);
    }

    // Общее пространство файловой системы
    *total = stats.f_blocks * stats.f_frsize;

    // Свободное пространство файловой системы
    unsigned long free_space = stats.f_bfree * stats.f_frsize;

    // Занятое пространство
    *used = *total - free_space;
}

// Функция для получения температуры процессора (в градусах Цельсия)
float get_cpu_temp() {
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) {
        perror("Failed to open /sys/class/thermal/thermal_zone0/temp");
        return -1.0;
    }

    int temp_millidegree;
    if (fscanf(fp, "%d", &temp_millidegree) != 1) {
        perror("Failed to read CPU temperature");
        fclose(fp);
        return -1.0;
    }
    fclose(fp);

    // Конвертация температуры из тысячных долей градуса в градусы
    return temp_millidegree / 1000.0;
}

// Функция для получения текущего использования оперативной памяти (в процентах)
float get_ram_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("Failed to open /proc/meminfo");
        return -1.0;
    }

    unsigned long mem_total = 0, mem_available = 0;
    char label[32];

    // Чтение значений MemTotal и MemAvailable из файла /proc/meminfo
    while (fscanf(fp, "%31s %lu kB", label, &mem_total) == 2) {
        if (strcmp(label, "MemTotal:") == 0) {
            mem_total = mem_available;
        } else if (strcmp(label, "MemAvailable:") == 0) {
            mem_available = mem_available;
        }
        if (mem_total && mem_available) {
            break;
        }
    }
    fclose(fp);

    if (!mem_total) {
        fprintf(stderr, "Failed to retrieve memory usage data\n");
        return -1.0;
    }

    // Вычисление использования памяти в процентах
    return ((mem_total - mem_available) / (float)mem_total) * 100.0;
}

// Функция для получения текущей нагрузки на CPU
float get_cpu_load() {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        perror("Failed to open /proc/loadavg");
        return -1.0;
    }

    float load;
    fscanf(fp, "%f", &load);
    fclose(fp);
    return load;
}

// Функция для отправки HTTP POST запроса с JSON-данными
void send_post_request(const char *url, const char *json_data) {
    CURL *curl;
    CURLcode res;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "curl_global_init() failed\n");
        exit(EXIT_FAILURE);
    }

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init() failed\n");
        curl_global_cleanup();
        exit(EXIT_FAILURE);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();
}

int main() {
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE); // Ошибка при создании процесса
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS); // Родительский процесс завершает выполнение
    }

    // Создание нового сеанса
    pid_t sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE); // Ошибка при создании сеанса
    }

    // Изменение текущего рабочего каталога
    if (chdir("/") < 0) {
        exit(EXIT_FAILURE);
    }

    // Закрытие стандартных файловых дескрипторов
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Открытие лог-файла для записи
    FILE *log_file = fopen("/var/log/my_monitord.log", "a");
    if (log_file) {
        fprintf(log_file, "Daemon started\n");
    }

    char time_str[64];
    char json_data[JSON_BUFFER_SIZE];
    char hostname[HOSTNAME_BUFFER_SIZE];

    while (1) {
        get_current_time(time_str, sizeof(time_str));
        get_hostname(hostname, sizeof(hostname));

        float cpu_load = get_cpu_load();
        float cpu_temp = get_cpu_temp();
        float ram_usage = get_ram_usage();

        unsigned long disk_used, disk_total;
        get_mem_usage("/", &disk_used, &disk_total);

        snprintf(json_data, sizeof(json_data),
                 "{\"hostname\": \"%s\", \"time\": \"%s\", \"cpu_load\": %.2f, \"cpu_temp\": %.2f, "
                 "\"ram_usage\": %.2f, \"disk_usage\": %.2f, \"disk_total\": %.2f}",
                 hostname, time_str, cpu_load, cpu_temp, ram_usage,
                 disk_used / (1024.0 * 1024.0 * 1024.0), // Конвертируем в GB
                 disk_total / (1024.0 * 1024.0 * 1024.0));


        send_post_request(URL, json_data);

        sleep(SLEEP_TIME);
    }
    
    exit(EXIT_SUCCESS);
}
