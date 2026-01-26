#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define READERS_COUNT 10
#define BUF_SIZE 64

static char shared_buf[BUF_SIZE];
static long counter = 0;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void* writer_thread(void* arg) {
    (void)arg;

    while (1) {
        // Формируем новое значение вне критической секции
        // (но сам counter и запись в shared_buf защищаем)
        char local[BUF_SIZE];

        pthread_mutex_lock(&mtx);
        counter += 1;
        snprintf(local, sizeof(local), "%ld", counter);
        // копируем в общий буфер
        strncpy(shared_buf, local, BUF_SIZE - 1);
        shared_buf[BUF_SIZE - 1] = '\0';
        pthread_mutex_unlock(&mtx);

        // Небольшая пауза, чтобы читатели успевали печатать
        usleep(200 * 1000); // 200 ms
    }

    return NULL;
}

static void* reader_thread(void* arg) {
    long idx = (long)arg;

    while (1) {
        char local[BUF_SIZE];

        pthread_mutex_lock(&mtx);
        // читаем общий буфер в локальный
        strncpy(local, shared_buf, BUF_SIZE - 1);
        local[BUF_SIZE - 1] = '\0';
        pthread_mutex_unlock(&mtx);

        // pthread_self() возвращает pthread_t (тип может быть не integer),
        // но для учебных задач обычно печатают как unsigned long.
        printf("[reader #%ld] tid=%lu | shared_buf=\"%s\"\n",
               idx, (unsigned long)pthread_self(), local);
        fflush(stdout);

        usleep(300 * 1000); // 300 ms
    }

    return NULL;
}

int main(void) {
    pthread_t writer;
    pthread_t readers[READERS_COUNT];

    // Инициализируем буфер стартовым значением
    pthread_mutex_lock(&mtx);
    snprintf(shared_buf, sizeof(shared_buf), "0");
    counter = 0;
    pthread_mutex_unlock(&mtx);

    // Запускаем writer
    if (pthread_create(&writer, NULL, writer_thread, NULL) != 0) {
        perror("pthread_create writer");
        return 1;
    }

    // Запускаем readers
    for (long i = 0; i < READERS_COUNT; i++) {
        if (pthread_create(&readers[i], NULL, reader_thread, (void *)i) != 0) {
            perror("pthread_create reader");
            return 1;
        }
    }

    // Программа демонстрационная и работает бесконечно.
    // join не нужен, destroy тоже не нужен при PTHREAD_MUTEX_INITIALIZER.
    for (;;) {
        pause(); // ждать сигналов, процесс живёт
    }

    return 0;
}

