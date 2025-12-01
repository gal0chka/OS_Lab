#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#define COLOR_BLUE   "\033[34m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_RESET  "\033[0m"

typedef struct {
    char *name;              // имя для вывода
    char *fullpath;          // путь для stat()
    struct stat st;
    int is_symlink;
    char link_target[PATH_MAX];
    ssize_t link_len;
} Entry;

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int cmp_entries(const void *a, const void *b) {
    const Entry *ea = (const Entry *)a;
    const Entry *eb = (const Entry *)b;
    return strcmp(ea->name, eb->name);
}

static void mode_to_string(mode_t mode, char *buf) {
    // тип файла
    if (S_ISREG(mode)) buf[0] = '-';
    else if (S_ISDIR(mode)) buf[0] = 'd';
    else if (S_ISLNK(mode)) buf[0] = 'l';
    else if (S_ISCHR(mode)) buf[0] = 'c';
    else if (S_ISBLK(mode)) buf[0] = 'b';
    else if (S_ISFIFO(mode)) buf[0] = 'p';
    else if (S_ISSOCK(mode)) buf[0] = 's';
    else buf[0] = '?';

    // права
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? 'x' : '-';

    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? 'x' : '-';

    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? 'x' : '-';

    buf[10] = '\0';
}

static const char *color_for_entry(const Entry *e) {
    mode_t m = e->st.st_mode;
    if (e->is_symlink) {
        return COLOR_CYAN;
    } else if (S_ISDIR(m)) {
        return COLOR_BLUE;
    } else if (S_ISREG(m) && (m & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return COLOR_GREEN;
    }
    return NULL; // обычный файл — без цвета
}

static void print_entry_short(const Entry *e) {
    const char *color = color_for_entry(e);
    if (color) {
        printf("%s%s%s\n", color, e->name, COLOR_RESET);
    } else {
        printf("%s\n", e->name);
    }
}

typedef struct {
    int w_links;
    int w_user;
    int w_group;
    int w_size;
    long long total_blocks;
} Widths;

static void compute_widths(Entry *entries, size_t n, Widths *w) {
    w->w_links = 0;
    w->w_user = 0;
    w->w_group = 0;
    w->w_size = 0;
    w->total_blocks = 0;

    char buf[64];

    for (size_t i = 0; i < n; ++i) {
        struct stat *st = &entries[i].st;

        // links
        int len = snprintf(buf, sizeof(buf), "%lu", (unsigned long)st->st_nlink);
        if (len > w->w_links) w->w_links = len;

        // user
        const char *uname = NULL;
        struct passwd *pw = getpwuid(st->st_uid);
        if (pw) uname = pw->pw_name;
        else {
            snprintf(buf, sizeof(buf), "%u", st->st_uid);
            uname = buf;
        }
        int luser = (int)strlen(uname);
        if (luser > w->w_user) w->w_user = luser;

        // group
        const char *gname = NULL;
        struct group *gr = getgrgid(st->st_gid);
        if (gr) gname = gr->gr_name;
        else {
            snprintf(buf, sizeof(buf), "%u", st->st_gid);
            gname = buf;
        }
        int lgroup = (int)strlen(gname);
        if (lgroup > w->w_group) w->w_group = lgroup;

        // size
        len = snprintf(buf, sizeof(buf), "%lld", (long long)st->st_size);
        if (len > w->w_size) w->w_size = len;

        // blocks (переведём в 1K-блоки из 512-байтных)
        w->total_blocks += (long long)(st->st_blocks / 2);
    }
}

static void print_entry_long(const Entry *e, const Widths *w) {
    char modebuf[11];
    mode_to_string(e->st.st_mode, modebuf);

    // владелец
    char userbuf[64];
    const char *uname = NULL;
    struct passwd *pw = getpwuid(e->st.st_uid);
    if (pw) uname = pw->pw_name;
    else {
        snprintf(userbuf, sizeof(userbuf), "%u", e->st.st_uid);
        uname = userbuf;
    }

    // группа
    char groupbuf[64];
    const char *gname = NULL;
    struct group *gr = getgrgid(e->st.st_gid);
    if (gr) gname = gr->gr_name;
    else {
        snprintf(groupbuf, sizeof(groupbuf), "%u", e->st.st_gid);
        gname = groupbuf;
    }

    // время
    char timebuf[64];
    struct tm lt;
    localtime_r(&e->st.st_mtime, &lt);
    strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", &lt);

    const char *color = color_for_entry(e);

    printf("%s %*lu %-*s %-*s %*lld %s ",
           modebuf,
           w->w_links, (unsigned long)e->st.st_nlink,
           w->w_user,  uname,
           w->w_group, gname,
           w->w_size,  (long long)e->st.st_size,
           timebuf);

    if (color) {
        printf("%s%s%s", color, e->name, COLOR_RESET);
    } else {
        printf("%s", e->name);
    }

    if (e->is_symlink && e->link_len > 0) {
        printf(" -> %s", e->link_target);
    }

    putchar('\n');
}

static void free_entries(Entry *entries, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        free(entries[i].name);
        free(entries[i].fullpath);
    }
    free(entries);
}

static void add_entry(Entry **entries, size_t *cnt, size_t *cap,
                      const char *dirpath, const char *name) {
    if (*cnt == *cap) {
        size_t new_cap = (*cap == 0) ? 32 : (*cap * 2);
        Entry *tmp = realloc(*entries, new_cap * sizeof(Entry));
        if (!tmp) die("realloc");
        *entries = tmp;
        *cap = new_cap;
    }

    Entry *e = &(*entries)[*cnt];
    memset(e, 0, sizeof(*e));

    e->name = strdup(name);
    if (!e->name) die("strdup");

    // формируем fullpath = dirpath + '/' + name
    size_t len_dir = strlen(dirpath);
    int need_slash = (len_dir > 0 && dirpath[len_dir - 1] != '/');

    size_t full_len = len_dir + (need_slash ? 1 : 0) + strlen(name) + 1;
    e->fullpath = malloc(full_len);
    if (!e->fullpath) die("malloc");

    strcpy(e->fullpath, dirpath);
    if (need_slash) strcat(e->fullpath, "/");
    strcat(e->fullpath, name);

    struct stat stbuf;
    if (lstat(e->fullpath, &stbuf) == -1) {
        fprintf(stderr, "myls: cannot stat '%s': %s\n", e->fullpath, strerror(errno));
        free(e->name);
        free(e->fullpath);
        return; // просто пропускаем, не увеличиваем *cnt
    }

    e->st = stbuf;
    e->is_symlink = S_ISLNK(stbuf.st_mode);
    e->link_len = 0;

    if (e->is_symlink) {
        ssize_t r = readlink(e->fullpath, e->link_target, sizeof(e->link_target) - 1);
        if (r >= 0) {
            e->link_target[r] = '\0';
            e->link_len = r;
        }
    }

    (*cnt)++;
}

static void list_directory(const char *path, bool flag_a, bool flag_l, bool print_header, bool multiple) {
    DIR *dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "myls: cannot open directory '%s': %s\n", path, strerror(errno));
        return;
    }

    if (print_header && multiple) {
        printf("%s:\n", path);
    }

    Entry *entries = NULL;
    size_t cnt = 0, cap = 0;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        const char *name = de->d_name;
        if (!flag_a) {
            if (name[0] == '.') continue; // скрытые не показываем
        }
        add_entry(&entries, &cnt, &cap, path, name);
    }

    closedir(dir);

    qsort(entries, cnt, sizeof(Entry), cmp_entries);

    if (flag_l) {
        Widths w;
        compute_widths(entries, cnt, &w);
        printf("total %lld\n", w.total_blocks);
        for (size_t i = 0; i < cnt; ++i) {
            print_entry_long(&entries[i], &w);
        }
    } else {
        for (size_t i = 0; i < cnt; ++i) {
            print_entry_short(&entries[i]);
        }
    }

    free_entries(entries, cnt);

    if (multiple) {
        putchar('\n');
    }
}

static void print_single_path(const char *path, bool flag_l) {
    Entry e;
    memset(&e, 0, sizeof(e));

    e.name = strdup(path);
    if (!e.name) die("strdup");

    e.fullpath = strdup(path);
    if (!e.fullpath) die("strdup");

    if (lstat(path, &e.st) == -1) {
        fprintf(stderr, "myls: cannot access '%s': %s\n", path, strerror(errno));
        free(e.name);
        free(e.fullpath);
        return;
    }

    e.is_symlink = S_ISLNK(e.st.st_mode);
    if (e.is_symlink) {
        ssize_t r = readlink(path, e.link_target, sizeof(e.link_target) - 1);
        if (r >= 0) {
            e.link_target[r] = '\0';
            e.link_len = r;
        }
    }

    if (flag_l) {
        Widths w;
        compute_widths(&e, 1, &w);
        print_entry_long(&e, &w);
    } else {
        print_entry_short(&e);
    }

    free(e.name);
    free(e.fullpath);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-l] [-a] [FILE...]\n", prog);
}

int main(int argc, char **argv) {
    bool flag_l = false;
    bool flag_a = false;

    int opt;
    while ((opt = getopt(argc, argv, "la")) != -1) {
        switch (opt) {
            case 'l':
                flag_l = true;
                break;
            case 'a':
                flag_a = true;
                break;
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    int n_paths = argc - optind;
    char **paths = argv + optind;

    if (n_paths == 0) {
        // по умолчанию — текущий каталог
        list_directory(".", flag_a, flag_l, false, false);
        return 0;
    }

    // Определим, много ли каталогов/путей
    bool multiple = (n_paths > 1);

    for (int i = 0; i < n_paths; ++i) {
        const char *p = paths[i];

        struct stat st;
        if (lstat(p, &st) == -1) {
            fprintf(stderr, "myls: cannot access '%s': %s\n", p, strerror(errno));
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            list_directory(p, flag_a, flag_l, true, multiple);
        } else {
            // обычный файл/ссылка
            print_single_path(p, flag_l);
        }
    }

    return 0;
}
