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
#define LOG_FILE "/var/log/my_monitord.log"

// Функция для получения текущего времени в виде строки
void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    strftime(buffer, size, TIME_FORMAT, local_time);
}

// Функция для записи ошибок в лог-файл
void log_error(const char *message) {
    char time_str[64];
    get_current_time(time_str, sizeof(time_str));
    
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        fprintf(log_file, "[%s] ERROR: %s\n", time_str, message);
        fclose(log_file);
    }
}

// Функция для получения имени хоста устройства
void get_hostname(char *buffer, size_t size) {
    if (gethostname(buffer, size) != 0) {
        log_error("Failed to get hostname");
        strncpy(buffer, "unknown", size);
    }
}

// Функция для получения использования памяти на файловой системе
void get_mem_usage(const char *path, unsigned long *used, unsigned long *total) {
    struct statvfs stats;

    if (statvfs(path, &stats) != 0) {
        log_error("Failed to get filesystem statistics");
        *used = 0;
        *total = 0;
        return;
    }

    *total = stats.f_blocks * stats.f_frsize;
    unsigned long free_space = stats.f_bfree * stats.f_frsize;
    *used = *total - free_space;
}

float get_ram_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        log_error("Failed to open /proc/meminfo");
        return -1.0;
    }

    unsigned long mem_total = 0, mem_available = 0;
    char label[32];

    // Чтение значений MemTotal и MemAvailable из файла /proc/meminfo
    while (fscanf(fp, "%31s %lu kB", label, &mem_available) == 2) {
        if (strcmp(label, "MemTotal:") == 0) {
            mem_total = mem_available;
        } else if (strcmp(label, "MemAvailable:") == 0) {
            mem_available = mem_available;
        }

        // Проверяем, что оба значения считаны
        if (mem_total > 0 && mem_available > 0) {
            break;
        }
    }
    fclose(fp);

    if (mem_total == 0 || mem_available == 0) {
        log_error("Failed to retrieve memory usage data: mem_total or mem_available is zero");
        return -1.0;
    }

    if (mem_total <= mem_available) {
        log_error("Invalid memory data: mem_total <= mem_available");
        return -1.0;
    }

    // Вычисление использования памяти в процентах
    float usage = ((mem_total - mem_available) / (float)mem_total) * 100.0;
    return usage;
}


// Функция для получения температуры процессора
float get_cpu_temp() {
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) {
        log_error("Failed to open /sys/class/thermal/thermal_zone0/temp");
        return -1.0;
    }

    int temp_millidegree;
    if (fscanf(fp, "%d", &temp_millidegree) != 1) {
        log_error("Failed to read CPU temperature");
        fclose(fp);
        return -1.0;
    }
    fclose(fp);

    return temp_millidegree / 1000.0;
}

// Функция для получения текущей нагрузки на CPU
float get_cpu_load() {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        log_error("Failed to open /proc/loadavg");
        return -1.0;
    }

    float load;
    if (fscanf(fp, "%f", &load) != 1) {
        log_error("Failed to read CPU load");
        fclose(fp);
        return -1.0;
    }
    fclose(fp);
    return load;
}

// Функция для отправки HTTP POST запроса с JSON-данными
void send_post_request(const char *url, const char *json_data) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("curl_easy_init() failed");
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        char error_message[256];
        snprintf(error_message, sizeof(error_message), "curl_easy_perform() failed: %s", curl_easy_strerror(res));
        log_error(error_message);
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}

int main() {
    // Демонизация процесса
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // Родитель завершает работу
    }

    if (setsid() < 0) {
        log_error("Failed to create new session");
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        log_error("Failed to change directory to /");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);

    // Перенаправление stderr в лог-файл
    int log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (log_fd < 0) {
        exit(EXIT_FAILURE); // Не удалось открыть лог-файл
    }
    dup2(log_fd, STDERR_FILENO);
    close(log_fd);

    char time_str[64];
    char hostname[HOSTNAME_BUFFER_SIZE];
    char json_data[JSON_BUFFER_SIZE];

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
                 disk_used / (1024.0 * 1024.0 * 1024.0),
                 disk_total / (1024.0 * 1024.0 * 1024.0));

        send_post_request(URL, json_data);
        sleep(SLEEP_TIME);
    }

    return 0;
}
