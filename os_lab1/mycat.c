#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h> 

// Совместимая реализация getline для Windows
static ssize_t portable_getline(char **lineptr, size_t *n, FILE *stream) {
    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (*lineptr == NULL) return -1;
    }
    
    size_t i = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (i + 1 >= *n) {
            size_t new_size = *n * 2;
            char *new_line = realloc(*lineptr, new_size);
            if (new_line == NULL) return -1;
            *lineptr = new_line;
            *n = new_size;
        }
        
        (*lineptr)[i++] = (char)c;
        if (c == '\n') break;
    }
    
    if (i == 0) return -1;
    
    (*lineptr)[i] = '\0';
    return (ssize_t)i;
}

static void print_file(FILE *fp, const char *name, bool flag_n, bool flag_b, bool flag_E) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;  // меняем size_t на ssize_t
    unsigned long long lineno = 1;

    while ((len = portable_getline(&line, &cap, fp)) != -1) {
        int is_empty = (len == 0) || (len == 1 && line[0] == '\n');
        int number_this = 0;

        if (flag_b) {
            number_this = !is_empty;       // -b нумерует только непустые
        } else if (flag_n) {
            number_this = 1;               // -n нумерует все
        }

        if (number_this) {
            // Формат как у cat -n: ширина 6 + таб
            printf("%6llu\t", lineno++);
        }

        if (flag_E) {
            // Показываем $ в конце строки
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
                printf("%s$\n", line);
                line[len-1] = '\n';
            } else {
                // строка без завершающего \n
                printf("%s$", line);
            }
        } else {
            fwrite(line, 1, (size_t)len, stdout);
        }
    }

    if (ferror(fp)) {
        fprintf(stderr, "mycat: read error on '%s': %s\n", name ? name : "stdin", strerror(errno));
    }

    free(line);
}

// Остальной код без изменений...
int main(int argc, char **argv) {
    bool flag_n = false, flag_b = false, flag_E = false;

    // разбор коротких флагов в стиле -n -b -E и их комбинаций (-nE, -bE и т.п.)
    int i = 1;
    for (; i < argc; ++i) {
        const char *a = argv[i];
        if (a[0] != '-') break;
        if (strcmp(a, "--") == 0) { i++; break; }
        for (size_t j = 1; a[j]; ++j) {
            if (a[j] == 'n') flag_n = true;
            else if (a[j] == 'b') flag_b = true;
            else if (a[j] == 'E') flag_E = true;
            else {
                fprintf(stderr, "mycat: unknown option -- %c\n", a[j]);
                fprintf(stderr, "Usage: mycat [-n] [-b] [-E] [FILE ...]\n");
                return 1;
            }
        }
    }

    // если файлов нет — читаем stdin
    if (i >= argc) {
        print_file(stdin, NULL, flag_n, flag_b, flag_E);
        return ferror(stdout) ? 1 : 0;
    }

    int exit_code = 0;
    for (; i < argc; ++i) {
        const char *fname = argv[i];
        if (strcmp(fname, "-") == 0) {
            print_file(stdin, NULL, flag_n, flag_b, flag_E);
            continue;
        }
        FILE *fp = fopen(fname, "r");
        if (!fp) {
            fprintf(stderr, "mycat: cannot open '%s': %s\n", fname, strerror(errno));
            exit_code = 1;
            continue;
        }
        print_file(fp, fname, flag_n, flag_b, flag_E);
        fclose(fp);
    }

    if (ferror(stdout)) exit_code = 1;
    return exit_code;
}