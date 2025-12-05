#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define SHM_NAME "/time_shm_example_v1"
#define SEM_NAME "/time_sem_example_v1"
#define LOCK_FILE "/tmp/time_sender_example.lock"

#define TIME_STR_LEN 64

typedef struct {
    pid_t sender_pid;
    long  seq;
    char  time_str[TIME_STR_LEN];
} shm_data_t;

static int shm_fd = -1;
static shm_data_t *data = NULL;
static sem_t *sem = SEM_FAILED;
static int lock_fd = -1;

static void cleanup(void) {
    if (data && data != MAP_FAILED) {
        munmap(data, sizeof(shm_data_t));
        data = NULL;
    }

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
        // Удаляем объект shared memory
        shm_unlink(SHM_NAME);
    }

    if (sem != SEM_FAILED) {
        sem_close(sem);
        sem = SEM_FAILED;
        // Удаляем именованный семафор
        sem_unlink(SEM_NAME);
    }

    if (lock_fd != -1) {
        // flock снимется автоматически при close/exit,
        // но закроем аккуратно
        close(lock_fd);
        lock_fd = -1;
        // файл можно оставить; это не мешает, потому что
        // важен именно advisory lock
    }
}

static void handle_sigint(int sig) {
    (void)sig;
    cleanup();
    _exit(0);
}

static void format_now(char *buf, size_t buflen) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tmv);
}

int main(void) {
    // === 1) Single-instance lock ===
    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lock_fd == -1) {
        perror("open lock file");
        return 1;
    }

    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            fprintf(stderr,
                    "Отправитель уже запущен (не удалось получить lock).\n");
        } else {
            perror("flock");
        }
        close(lock_fd);
        return 1;
    }

    // === 2) Создаем/открываем shared memory ===
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        cleanup();
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(shm_data_t)) == -1) {
        perror("ftruncate");
        cleanup();
        return 1;
    }

    data = mmap(NULL, sizeof(shm_data_t),
                PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        cleanup();
        return 1;
    }

    // Инициализация структуры
    memset(data, 0, sizeof(*data));
    data->sender_pid = getpid();
    data->seq = 0;
    snprintf(data->time_str, TIME_STR_LEN, "not set yet");

    // === 3) Именованный семафор-мьютекс ===
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open");
        cleanup();
        return 1;
    }

    // === 4) Обработчики сигналов для аккуратной очистки ===
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Sender started. pid=%d\n", getpid());
    printf("Shared memory: %s, semaphore: %s\n", SHM_NAME, SEM_NAME);

    // === 5) Бесконечный цикл записи ===
    while (1) {
        char tbuf[TIME_STR_LEN];
        format_now(tbuf, sizeof(tbuf));

        if (sem_wait(sem) == -1) {
            perror("sem_wait");
            break;
        }

        data->sender_pid = getpid();
        data->seq += 1;
        strncpy(data->time_str, tbuf, TIME_STR_LEN - 1);
        data->time_str[TIME_STR_LEN - 1] = '\0';

        if (sem_post(sem) == -1) {
            perror("sem_post");
            break;
        }

        sleep(1);
    }

    cleanup();
    return 0;
}
