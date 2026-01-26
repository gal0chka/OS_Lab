#define _XOPEN_SOURCE 700

#include <errno.h>
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

static void format_now(char *buf, size_t buflen) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, buflen, "%Y-%m-%d %H:%M:%S", &tmv);
}

static int sem_lock(int semid_) {
    struct sembuf op = {0, -1, SEM_UNDO};
    return semop(semid_, &op, 1);
}

static int sem_unlock(int semid_) {
    struct sembuf op = {0, 1, SEM_UNDO};
    return semop(semid_, &op, 1);
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

    // Открываем существующий shm (без IPC_CREAT)
    int shmid = shmget(shm_key, sizeof(shm_data_t), 0666);
    if (shmid == -1) {
        fprintf(stderr, "Receiver: shared memory not found. Start sender first.\n");
        perror("shmget");
        return 1;
    }

    shm_data_t *data = (shm_data_t *)shmat(shmid, NULL, SHM_RDONLY);
    if (data == (shm_data_t *)-1) {
        perror("shmat");
        return 1;
    }

    // Открываем существующий semaphore set (без IPC_CREAT)
    int semid = semget(sem_key, 1, 0666);
    if (semid == -1) {
        fprintf(stderr, "Receiver: semaphore not found. Start sender first.\n");
        perror("semget");
        shmdt(data);
        return 1;
    }

    printf("Receiver started. pid=%d shmid=%d semid=%d\n", (int)getpid(), shmid, semid);

    while (1) {
        char self_time[TIME_STR_LEN];
        format_now(self_time, sizeof(self_time));

        pid_t sender_pid;
        long seq;
        char sender_time[TIME_STR_LEN];

        if (sem_lock(semid) == -1) {
            // sender мог удалить IPC (IPC_RMID) => выходим красиво
            if (errno == EIDRM || errno == EINVAL) {
                fprintf(stderr, "Receiver: semaphore removed (sender stopped). Exiting.\n");
                break;
            }
            perror("semop lock");
            break;
        }

        sender_pid = data->sender_pid;
        seq = data->seq;
        strncpy(sender_time, data->time_str, TIME_STR_LEN - 1);
        sender_time[TIME_STR_LEN - 1] = '\0';

        if (sem_unlock(semid) == -1) {
            if (errno == EIDRM || errno == EINVAL) {
                fprintf(stderr, "Receiver: semaphore removed while unlocking. Exiting.\n");
                break;
            }
            perror("semop unlock");
            break;
        }

        printf("Self: pid=%d time=%s | Received: pid=%d time=%s seq=%ld\n",
               (int)getpid(), self_time, (int)sender_pid, sender_time, seq);
        fflush(stdout);

        sleep(1);
    }

    shmdt(data);
    return 0;
}
