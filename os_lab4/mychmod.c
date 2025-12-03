#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>


static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s MODE FILE...\n", prog);
    fprintf(stderr, "MODE: [ugoa]*[+-=][rwx]+  или  восьмеричное число (например, 766)\n");
}

/* Проверка: строка полностью состоит из 3 восьмеричных цифр */
static int is_octal_mode(const char *s) {
    size_t len = strlen(s);
    if (len != 3) return 0;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '7') return 0;
    }
    return 1;
}

/* Разбор восьмеричного режима: XYZ -> 0XYZ в виде mode_t */
static mode_t parse_octal_mode(const char *s) {
    // s уже проверена на [0-7]{3}
    int u = s[0] - '0';
    int g = s[1] - '0';
    int o = s[2] - '0';

    mode_t mode = 0;
    // user
    if (u & 4) mode |= S_IRUSR;
    if (u & 2) mode |= S_IWUSR;
    if (u & 1) mode |= S_IXUSR;
    // group
    if (g & 4) mode |= S_IRGRP;
    if (g & 2) mode |= S_IWGRP;
    if (g & 1) mode |= S_IXGRP;
    // others
    if (o & 4) mode |= S_IROTH;
    if (o & 2) mode |= S_IWOTH;
    if (o & 1) mode |= S_IXOTH;

    return mode;
}

/*
 * Разбор символьного режима:
 *   [ugoa]*[+-=][rwx]+
 * Если [ugoa] не указаны — по умолчанию 'a' (ugo).
 */
static int apply_symbolic_mode(const char *spec, mode_t old_mode, mode_t *new_mode) {
    const char *p = spec;

    int who_u = 0, who_g = 0, who_o = 0;
    int saw_who = 0;

    // 1. who
    while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
        saw_who = 1;
        if (*p == 'u') who_u = 1;
        else if (*p == 'g') who_g = 1;
        else if (*p == 'o') who_o = 1;
        else if (*p == 'a') who_u = who_g = who_o = 1;
        p++;
    }

    if (!saw_who) {
        // по умолчанию — a (ugo)
        who_u = who_g = who_o = 1;
    }

    // 2. операция
    char op = *p;
    if (op != '+' && op != '-' && op != '=') {
        fprintf(stderr, "mychmod: invalid symbolic mode (expected +, -, =): '%s'\n", spec);
        return -1;
    }
    p++;

    if (*p == '\0') {
        fprintf(stderr, "mychmod: missing permission part in mode: '%s'\n", spec);
        return -1;
    }

    // 3. permissions (rwx)+
    mode_t perm_u = 0, perm_g = 0, perm_o = 0;

    for (; *p; ++p) {
        char c = *p;
        if (c != 'r' && c != 'w' && c != 'x') {
            fprintf(stderr, "mychmod: invalid permission char '%c' in mode '%s'\n", c, spec);
            return -1;
        }

        if (c == 'r') {
            if (who_u) perm_u |= S_IRUSR;
            if (who_g) perm_g |= S_IRGRP;
            if (who_o) perm_o |= S_IROTH;
        } else if (c == 'w') {
            if (who_u) perm_u |= S_IWUSR;
            if (who_g) perm_g |= S_IWGRP;
            if (who_o) perm_o |= S_IWOTH;
        } else if (c == 'x') {
            if (who_u) perm_u |= S_IXUSR;
            if (who_g) perm_g |= S_IXGRP;
            if (who_o) perm_o |= S_IXOTH;
        }
    }

    mode_t mode = old_mode;

    mode_t mask_u = S_IRUSR | S_IWUSR | S_IXUSR;
    mode_t mask_g = S_IRGRP | S_IWGRP | S_IXGRP;
    mode_t mask_o = S_IROTH | S_IWOTH | S_IXOTH;

    if (op == '+') {
        // добавить биты
        mode |= perm_u | perm_g | perm_o;
    } else if (op == '-') {
        // убрать биты
        mode &= ~(perm_u | perm_g | perm_o);
    } else if (op == '=') {
        // очистить соответствующие области и установить только указанные
        if (who_u) {
            mode &= ~mask_u;
            mode |= perm_u;
        }
        if (who_g) {
            mode &= ~mask_g;
            mode |= perm_g;
        }
        if (who_o) {
            mode &= ~mask_o;
            mode |= perm_o;
        }
    }

    *new_mode = mode;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode_str = argv[1];

    int is_octal = is_octal_mode(mode_str);

    int exit_code = 0;

    for (int i = 2; i < argc; ++i) {
        const char *path = argv[i];
        struct stat st;

        if (stat(path, &st) == -1) {
            fprintf(stderr, "mychmod: cannot stat '%s': %s\n", path, strerror(errno));
            exit_code = 1;
            continue;
        }

        mode_t new_mode;

        if (is_octal) {
            mode_t perms = parse_octal_mode(mode_str);
            // сохраняем тип файла, меняем только биты прав
            new_mode = (st.st_mode & ~0777) | perms;
        } else {
            if (apply_symbolic_mode(mode_str, st.st_mode, &new_mode) != 0) {
                exit_code = 1;
                continue;
            }
        }

        if (chmod(path, new_mode) == -1) {
            fprintf(stderr, "mychmod: cannot chmod '%s': %s\n", path, strerror(errno));
            exit_code = 1;
            continue;
        }
    }

    return exit_code;
}
