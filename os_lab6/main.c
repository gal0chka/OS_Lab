#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // pipe, fork, read, write, close, sleep
#include <sys/types.h>
#include <sys/wait.h>   // waitpid
#include <sys/stat.h>   // mkfifo
#include <fcntl.h>      // open
#include <time.h>       // time, localtime, strftime
#include <string.h>
#include <errno.h>

static const char *FIFO_PATH = "./myfifo";

static void format_time(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) {
        snprintf(buf, sz, "time_error");
        return;
    }
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", tm);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Практика №6: pipe + fifo\n\n"
        "Использование:\n"
        "  %s pipe          - демонстрация pipe + fork\n"
        "  %s fifo-writer   - процесс-писатель в FIFO\n"
        "  %s fifo-reader   - процесс-читатель из FIFO\n"
        "  %s -h            - справка\n",
        prog, prog, prog, prog
    );
}

/* ===== Часть 1: pipe + fork ===== */

static int demo_pipe(void) {
    int fds[2];
    if (pipe(fds) < 0) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        // ==== Дочерний процесс ====
        close(fds[1]); // закрываем запись, оставляем только чтение

        char msg[256];
        ssize_t n = read(fds[0], msg, sizeof(msg) - 1);
        if (n < 0) {
            perror("[child] read");
            close(fds[0]);
            return EXIT_FAILURE;
        }
        msg[n] = '\0';

        // Ждём 5+ секунд, чтобы время отличалось
        sleep(5);

        char time_str[64];
        format_time(time_str, sizeof(time_str));

        printf("[child] PID=%d, PPID=%d, time=%s\n",
               (int)getpid(), (int)getppid(), time_str);
        printf("[child] Получено через pipe: %s\n", msg);

        close(fds[0]);
        return 0;
    } else {
        // ==== Родительский процесс ====
        close(fds[0]); // закрываем чтение, оставляем только запись

        char time_str[64];
        format_time(time_str, sizeof(time_str));

        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Сообщение от родителя: PID=%d, time=%s",
                 (int)getpid(), time_str);

        if (write(fds[1], msg, strlen(msg)) < 0) {
            perror("[parent] write");
            close(fds[1]);
            return EXIT_FAILURE;
        }

        printf("[parent] Отправил строку через pipe:\n");
        printf("         \"%s\"\n", msg);

        close(fds[1]);

        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0) {
            perror("waitpid");
            return EXIT_FAILURE;
        }

        if (WIFEXITED(status)) {
            printf("[parent] Дочерний процесс PID=%d завершился, код=%d\n",
                   (int)pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[parent] Дочерний процесс PID=%d завершился по сигналу %d\n",
                   (int)pid, WTERMSIG(status));
        } else {
            printf("[parent] Дочерний процесс PID=%d завершился непонятно (status=0x%x)\n",
                   (int)pid, status);
        }

        return 0;
    }
}

/* ===== Часть 2: FIFO + два отдельных процесса ===== */

static int fifo_writer(void) {
    // Создаём FIFO, если ещё нет
    if (mkfifo(FIFO_PATH, 0666) < 0) {
        if (errno != EEXIST) {
            perror("[writer] mkfifo");
            return EXIT_FAILURE;
        }
    }

    printf("[writer] Открываю FIFO '%s' на запись...\n", FIFO_PATH);
    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd < 0) {
        perror("[writer] open");
        return EXIT_FAILURE;
    }

    char time_str[64];
    format_time(time_str, sizeof(time_str));

    char msg[256];
    snprintf(msg, sizeof(msg),
             "Сообщение от writer: PID=%d, time=%s",
             (int)getpid(), time_str);

    ssize_t n = write(fd, msg, strlen(msg));
    if (n < 0) {
        perror("[writer] write");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("[writer] Отправил строку в FIFO:\n");
    printf("         \"%s\"\n", msg);

    close(fd);
    return 0;
}

static int fifo_reader(void) {
    // Создаём FIFO, если ещё нет (иначе open() упадёт с ENOENT)
    if (mkfifo(FIFO_PATH, 0666) < 0) {
        if (errno != EEXIST) {
            perror("[reader] mkfifo");
            return EXIT_FAILURE;
        }
    }

    printf("[reader] Открываю FIFO '%s' на чтение...\n", FIFO_PATH);
    int fd = open(FIFO_PATH, O_RDONLY);
    if (fd < 0) {
        perror("[reader] open");
        // если не удалось открыть — всё равно попробуем убрать FIFO (не критично)
        if (unlink(FIFO_PATH) < 0 && errno != ENOENT) {
            perror("[reader] unlink");
        }
        return EXIT_FAILURE;
    }

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("[reader] read");
        close(fd);
        if (unlink(FIFO_PATH) < 0 && errno != ENOENT) {
            perror("[reader] unlink");
        }
        return EXIT_FAILURE;
    }
    buf[n] = '\0';

    // Ждём 10+ секунд, чтобы время явно отличалось
    sleep(10);

    char time_str[64];
    format_time(time_str, sizeof(time_str));

    printf("[reader] PID=%d, time=%s\n", (int)getpid(), time_str);
    printf("[reader] Получено из FIFO: %s\n", buf);

    close(fd);

    // Требование лабы: после завершения FIFO должен быть удалён
    if (unlink(FIFO_PATH) < 0) {
        if (errno != ENOENT) {
            perror("[reader] unlink");
            return EXIT_FAILURE;
        }
    } else {
        printf("[reader] FIFO '%s' удалён (unlink)\n", FIFO_PATH);
    }

    return 0;
}


/* ===== main ===== */

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "pipe") == 0) {
        // Часть 1: pipe + fork
        return demo_pipe();
    }

    if (strcmp(argv[1], "fifo-writer") == 0) {
        // Часть 2: процесс-писатель
        return fifo_writer();
    }

    if (strcmp(argv[1], "fifo-reader") == 0) {
        // Часть 2: процесс-читатель
        return fifo_reader();
    }

    fprintf(stderr, "Неизвестный режим: %s\n", argv[1]);
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
