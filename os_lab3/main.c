#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <errno.h>


static void on_exit_handler(void) {
    // Вызывается при нормальном завершении процесса (exit / return из main)
    printf("[atexit] Процесс PID=%d завершает работу\n", getpid());
}

// Обработчик SIGINT, установленный через signal()
static void sigint_handler(int sig) {
    // strsignal() — текстовое описание сигнала
    printf("[PID %d] Пойман сигнал SIGINT (%d): %s\n",
           getpid(), sig, strsignal(sig));
}

// Обработчик SIGTERM, установленный через sigaction()
static void sigterm_handler(int sig) {
    printf("[PID %d] Пойман сигнал SIGTERM (%d): %s\n",
           getpid(), sig, strsignal(sig));
}

int main(void) {
    // 1. Регистрируем обработчик atexit()
    if (atexit(on_exit_handler) != 0) {
        fprintf(stderr, "Не удалось зарегистрировать atexit(): %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    // 2. Переопределяем SIGINT через signal()
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal(SIGINT)");
        return EXIT_FAILURE;
    }

    // 3. Переопределяем SIGTERM через sigaction()
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction(SIGTERM)");
        return EXIT_FAILURE;
    }

    // 4. Вызов fork()
    pid_t pid = fork();
    if (pid < 0) {
        // Ошибка
        perror("fork");
        return EXIT_FAILURE;
    } else if (pid == 0) {
        // === Дочерний процесс ===
        pid_t my_pid  = getpid();
        pid_t my_ppid = getppid();

        printf("[child] Я дочерний процесс. PID=%d, PPID=%d\n",
               my_pid, my_ppid);
        printf("[child] Посплю 30 секунд, можно отправить мне SIGINT/SIGTERM (kill -INT %d / kill -TERM %d)\n",
               my_pid, my_pid);

        // Имитация работы: ждём, чтобы можно было руками послать сигналы
        sleep(30);

        printf("[child] Завершаюсь с кодом 42\n");
        // atexit() отработает перед выходом
        exit(42);
    } else {
        // === Родительский процесс ===
        pid_t my_pid  = getpid();
        pid_t my_ppid = getppid();

        printf("[parent] Я родительский процесс. PID=%d, PPID=%d, PID дочернего=%d\n",
               my_pid, my_ppid, pid);
        printf("[parent] Ожидаю завершения дочернего процесса...\n");

        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        if (w == -1) {
            perror("waitpid");
            return EXIT_FAILURE;
        }

        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            printf("[parent] Дочерний процесс PID=%d завершился нормально, код=%d\n",
                   pid, code);
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            printf("[parent] Дочерний процесс PID=%d завершился по сигналу %d (%s)\n",
                   pid, sig, strsignal(sig));
        } else {
            printf("[parent] Дочерний процесс PID=%d завершился непонятным образом (status=0x%x)\n",
                   pid, status);
        }

        printf("[parent] Родительский процесс main() заканчивается\n");
        // atexit() тоже отработает при возврате из main
        return 0;
    }
}
