#define _XOPEN_SOURCE 700

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define FTOK_PATH       "."
#define FTOK_PROJ_SHM   'S'
#define FTOK_PROJ_SEM   'M'

#define TIME_STR_LEN 64

typedef struct {
    pid_t sender_pid;
    long  seq;
    char  time_str[TIME_STR_LEN];
} shm_data_t;

/* semctl SETVAL на Linux требует union semun */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int shmid = -1;
static int semid = -1;
static shm_data_t *data = (shm_data_t *)-1;

static void format_now(char *buf, size_t buflen) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tmv);
}

static int sem_lock(int semid_) {
    struct sembuf op = {0, -1, SEM_UNDO}; // P()
    return semop(semid_, &op, 1);
}

static int sem_unlock(int semid_) {
    struct sembuf op = {0, 1, SEM_UNDO}; // V()
    return semop(semid_, &op, 1);
}

static void cleanup(void) {
    if (data != (shm_data_t *)-1) {
        shmdt(data);
        data = (shm_data_t *)-1;
    }
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
        shmid = -1;
    }
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
        semid = -1;
    }
}

static void handle_sig(int sig) {
    (void)sig;
    cleanup();
    _exit(0);
}

int main(void) {
    key_t shm_key = ftok(FTOK_PATH, FTOK_PROJ_SHM);
    if (shm_key == (key_t)-1) {
        perror("ftok(shm)");
        return 1;
    }

    key_t sem_key = ftok(FTOK_PATH, FTOK_PROJ_SEM);
    if (sem_key == (key_t)-1) {
        perror("ftok(sem)");
        return 1;
    }

    // 1) Shared memory
    shmid = shmget(shm_key, sizeof(shm_data_t), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        cleanup();
        return 1;
    }

    data = (shm_data_t *)shmat(shmid, NULL, 0);
    if (data == (shm_data_t *)-1) {
        perror("shmat");
        cleanup();
        return 1;
    }

    // 2) Semaphore set (1 semaphore as mutex)
    semid = semget(sem_key, 1, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget");
        cleanup();
        return 1;
    }

    // Инициализация семафора в 1. Для учебной лабы достаточно.
    union semun arg;
    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) == -1) {
        perror("semctl(SETVAL)");
        cleanup();
        return 1;
    }

    // 3) Signal handlers for cleanup
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // 4) Init shared data under lock
    if (sem_lock(semid) == -1) {
        perror("semop lock");
        cleanup();
        return 1;
    }
    memset(data, 0, sizeof(*data));
    data->sender_pid = getpid();
    data->seq = 0;
    snprintf(data->time_str, TIME_STR_LEN, "not set yet");
    if (sem_unlock(semid) == -1) {
        perror("semop unlock");
        cleanup();
        return 1;
    }

    printf("Sender started. pid=%d shmid=%d semid=%d\n", (int)getpid(), shmid, semid);
    printf("Run receiver in another terminal.\n");

    while (1) {
        char tbuf[TIME_STR_LEN];
        format_now(tbuf, sizeof(tbuf));

        if (sem_lock(semid) == -1) {
            perror("semop lock");
            break;
        }

        data->sender_pid = getpid();
        data->seq += 1;
        strncpy(data->time_str, tbuf, TIME_STR_LEN - 1);
        data->time_str[TIME_STR_LEN - 1] = '\0';

        if (sem_unlock(semid) == -1) {
            perror("semop unlock");
            break;
        }

        sleep(1);
    }

    cleanup();
    return 0;
}
