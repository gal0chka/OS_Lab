#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

static int grep_stream(FILE *fp, const char *srcname, const char *pattern, int print_prefix) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;  // меняем на ssize_t
    int matches = 0;

    while ((len = portable_getline(&line, &cap, fp)) != -1) {
        if (strstr(line, pattern) != NULL) {
            if (print_prefix && srcname) {
                printf("%s:", srcname);
            }
            fwrite(line, 1, (size_t)len, stdout);
            matches++;
        }
    }

    free(line);

    if (ferror(fp)) {
        fprintf(stderr, "mygrep: read error on '%s': %s\n", srcname ? srcname : "stdin", strerror(errno));
        return -1;
    }
    return matches;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mygrep PATTERN [FILE ...]\n");
        return 2; // ошибка использования
    }

    const char *pattern = argv[1];

    // только stdin (для туннелирования/пайпа)
    if (argc == 2) {
        int r = grep_stream(stdin, NULL, pattern, 0);
        if (r < 0) return 2;
        return (r > 0) ? 0 : 1;
    }

    int many_files = (argc - 2) > 1;
    int any_match = 0;
    int any_error = 0;

    for (int i = 2; i < argc; ++i) {
        const char *fname = argv[i];
        if (strcmp(fname, "-") == 0) {
            int r = grep_stream(stdin, NULL, pattern, 0);
            if (r < 0) any_error = 1; else if (r > 0) any_match = 1;
            continue;
        }
        FILE *fp = fopen(fname, "r");
        if (!fp) {
            fprintf(stderr, "mygrep: cannot open '%s': %s\n", fname, strerror(errno));
            any_error = 1;
            continue;
        }
        int r = grep_stream(fp, many_files ? fname : NULL, pattern, many_files);
        if (r < 0) any_error = 1; else if (r > 0) any_match = 1;
        fclose(fp);
    }

    if (any_error) return 2;
    return any_match ? 0 : 1;
}