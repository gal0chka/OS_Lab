// receiver.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>

#define SHM_NAME "/time_shm_example_v1"
#define SEM_NAME "/time_sem_example_v1"

#define TIME_STR_LEN 64

typedef struct {
    pid_t sender_pid;
    long  seq;
    char  time_str[TIME_STR_LEN];
} shm_data_t;

static void format_now(char *buf, size_t buflen) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tmv);
}

int main(void) {
    // Открываем shared memory только если она уже создана sender-ом
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (shm_fd == -1) {
        fprintf(stderr,
                "Не удалось открыть shared memory. Возможно, отправитель не запущен.\n");
        perror("shm_open");
        return 1;
    }

    shm_data_t *data = mmap(NULL, sizeof(shm_data_t),
                            PROT_READ, MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return 1;
    }

    // Открываем существующий семафор
    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        fprintf(stderr,
                "Не удалось открыть семафор. Возможно, отправитель не запущен.\n");
        perror("sem_open");
        munmap(data, sizeof(shm_data_t));
        close(shm_fd);
        return 1;
    }

    printf("Receiver started. pid=%d\n", getpid());

    while (1) {
        char self_time[TIME_STR_LEN];
        format_now(self_time, sizeof(self_time));

        pid_t sender_pid;
        long seq;
        char sender_time[TIME_STR_LEN];

        if (sem_wait(sem) == -1) {
            perror("sem_wait");
            break;
        }

        sender_pid = data->sender_pid;
        seq = data->seq;
        strncpy(sender_time, data->time_str, TIME_STR_LEN - 1);
        sender_time[TIME_STR_LEN - 1] = '\0';

        if (sem_post(sem) == -1) {
            perror("sem_post");
            break;
        }

        printf("Self: pid=%d time=%s | Received: pid=%d time=%s seq=%ld\n",
               getpid(), self_time, sender_pid, sender_time, seq);
        fflush(stdout);

        sleep(1);
    }

    sem_close(sem);
    munmap(data, sizeof(shm_data_t));
    close(shm_fd);
    return 0;
}
