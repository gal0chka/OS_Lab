#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_NAME_LEN 255
#define ARCH_MAGIC "MYARCH1"
#define ARCH_MAGIC_LEN 7

// Фиксируем формат заголовка на диске (без паддингов)
struct __attribute__((packed)) FileHeaderDisk {
    char     name[256];    // null-terminated
    uint64_t size;         // bytes
    uint32_t mode;         // st_mode (как минимум права)
    uint32_t uid;          // st_uid
    uint32_t gid;          // st_gid
    int64_t  atime;        // st_atime
    int64_t  mtime;        // st_mtime
    uint8_t  deleted;      // 0/1
    uint8_t  reserved[3];  // добивка до кратности (можно расширять)
};

_Static_assert(sizeof(struct FileHeaderDisk) == 296, "Header size must be 292 bytes");

static void print_help(const char *prog) {
    printf("Примитивный архиватор (без сжатия)\n\n");
    printf("Использование:\n");
    printf("  %s -h | --help\n", prog);
    printf("  %s ARCH -i|--input FILE [FILE...]\n", prog);
    printf("  %s ARCH -e|--extract FILE [FILE...]\n", prog);
    printf("  %s ARCH -s|--stat\n", prog);
    printf("\nПримеры:\n");
    printf("  %s myarch.bin -i file1.txt file2.txt\n", prog);
    printf("  %s myarch.bin -e file1.txt\n", prog);
    printf("  %s myarch.bin -s\n", prog);
}

static int write_full(int fd, const void *buf, size_t count) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t left = count;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }
    return 0;
}

// Возвращает:
// 0  — прочитали ровно count байт
// 1  — EOF ДО чтения (0 байт)
// 2  — "короткое" чтение (EOF посередине) => архив битый
// -1 — ошибка
static int read_full_exact(int fd, void *buf, size_t count) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;

    while (got < count) {
        ssize_t n = read(fd, p + got, count - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return (got == 0) ? 1 : 2;
        }
        got += (size_t)n;
    }
    return 0;
}

static int open_archive(const char *arch_name, int need_rw, int *fd_out) {
    int flags = need_rw ? (O_RDWR | O_CREAT) : O_RDONLY;
    int fd = open(arch_name, flags, 0644);
    if (fd < 0) {
        fprintf(stderr, "archiver: cannot open '%s': %s\n", arch_name, strerror(errno));
        return -1;
    }

    char magic[ARCH_MAGIC_LEN];
    ssize_t n = read(fd, magic, ARCH_MAGIC_LEN);

    if (n == 0) {
        // новый пустой файл
        if (!need_rw) {
            fprintf(stderr, "archiver: '%s' is empty and read-only\n", arch_name);
            close(fd);
            return -1;
        }
        if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
            fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
        if (write_full(fd, ARCH_MAGIC, ARCH_MAGIC_LEN) < 0) {
            fprintf(stderr, "archiver: cannot write magic to '%s': %s\n", arch_name, strerror(errno));
            close(fd);
            return -1;
        }
        // fd теперь на позиции после magic
    } else if (n == (ssize_t)ARCH_MAGIC_LEN) {
        if (memcmp(magic, ARCH_MAGIC, ARCH_MAGIC_LEN) != 0) {
            fprintf(stderr, "archiver: '%s' is not a valid archive\n", arch_name);
            close(fd);
            return -1;
        }
    } else if (n > 0 && n < (ssize_t)ARCH_MAGIC_LEN) {
        fprintf(stderr, "archiver: '%s' is corrupted (short magic)\n", arch_name);
        close(fd);
        return -1;
    } else {
        fprintf(stderr, "archiver: cannot read '%s': %s\n", arch_name, strerror(errno));
        close(fd);
        return -1;
    }

    *fd_out = fd;
    return 0;
}

static int name_in_list(const char *name, int argc, char **list) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp(name, list[i]) == 0) return 1;
    }
    return 0;
}

// Переписывает архив, удаляя:
// - все записи hdr.deleted==1
// - все записи, имя которых входит в remove_list (argc_remove)
static int compact_archive_remove(const char *arch_name, int argc_remove, char **remove_list) {
    int fd_in = -1;
    if (open_archive(arch_name, 0, &fd_in) < 0) {
        return 1;
    }

    // tmp рядом с архивом (в той же директории), чтобы rename был атомарным
    char tmp_name[1024];
    pid_t pid = getpid();
    snprintf(tmp_name, sizeof(tmp_name), "%s.tmp.%ld", arch_name, (long)pid);

    int fd_out = open(tmp_name, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        fprintf(stderr, "archiver: cannot create temp '%s': %s\n", tmp_name, strerror(errno));
        close(fd_in);
        return 1;
    }

    // magic
    if (write_full(fd_out, ARCH_MAGIC, ARCH_MAGIC_LEN) < 0) {
        fprintf(stderr, "archiver: cannot write magic to '%s': %s\n", tmp_name, strerror(errno));
        close(fd_in);
        close(fd_out);
        unlink(tmp_name);
        return 1;
    }

    if (lseek(fd_in, ARCH_MAGIC_LEN, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
        close(fd_in);
        close(fd_out);
        unlink(tmp_name);
        return 1;
    }

    struct FileHeaderDisk hdr;
    char buf[4096];

    while (1) {
        int r = read_full_exact(fd_in, &hdr, sizeof(hdr));
        if (r == 1) {
            // чистый EOF
            break;
        }
        if (r == 2) {
            fprintf(stderr, "archiver: corrupted archive '%s' (truncated header)\n", arch_name);
            close(fd_in);
            close(fd_out);
            unlink(tmp_name);
            return 1;
        }
        if (r < 0) {
            fprintf(stderr, "archiver: read error '%s': %s\n", arch_name, strerror(errno));
            close(fd_in);
            close(fd_out);
            unlink(tmp_name);
            return 1;
        }

        // Решение: копируем или пропускаем
        int drop = 0;
        if (hdr.deleted) drop = 1;
        if (!drop && argc_remove > 0 && name_in_list(hdr.name, argc_remove, remove_list)) drop = 1;

        uint64_t left = hdr.size;

        if (!drop) {
            // пишем заголовок
            if (write_full(fd_out, &hdr, sizeof(hdr)) < 0) {
                fprintf(stderr, "archiver: write error '%s': %s\n", tmp_name, strerror(errno));
                close(fd_in);
                close(fd_out);
                unlink(tmp_name);
                return 1;
            }
            // копируем данные
            while (left > 0) {
                size_t chunk = (left > sizeof(buf)) ? sizeof(buf) : (size_t)left;
                ssize_t n = read(fd_in, buf, chunk);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    fprintf(stderr, "archiver: read error '%s': %s\n", arch_name, strerror(errno));
                    close(fd_in);
                    close(fd_out);
                    unlink(tmp_name);
                    return 1;
                }
                if (n == 0) {
                    fprintf(stderr, "archiver: corrupted archive '%s' (truncated data)\n", arch_name);
                    close(fd_in);
                    close(fd_out);
                    unlink(tmp_name);
                    return 1;
                }
                if (write_full(fd_out, buf, (size_t)n) < 0) {
                    fprintf(stderr, "archiver: write error '%s': %s\n", tmp_name, strerror(errno));
                    close(fd_in);
                    close(fd_out);
                    unlink(tmp_name);
                    return 1;
                }
                left -= (uint64_t)n;
            }
        } else {
            // пропускаем данные
            if (lseek(fd_in, (off_t)left, SEEK_CUR) == (off_t)-1) {
                fprintf(stderr, "archiver: lseek skip failed '%s': %s\n", arch_name, strerror(errno));
                close(fd_in);
                close(fd_out);
                unlink(tmp_name);
                return 1;
            }
        }
    }

    // гарантируем запись на диск (опционально, но полезно)
    (void)fsync(fd_out);

    close(fd_in);
    close(fd_out);

    // атомарно заменяем архив
    if (rename(tmp_name, arch_name) < 0) {
        fprintf(stderr, "archiver: rename('%s','%s') failed: %s\n", tmp_name, arch_name, strerror(errno));
        unlink(tmp_name);
        return 1;
    }

    return 0;
}

static int do_input(const char *arch_name, int argc, char **files) {
    int fd_arch;
    if (open_archive(arch_name, 1, &fd_arch) < 0) {
        return 1;
    }

    int exit_code = 0;
    char buf[4096];

    for (int i = 0; i < argc; ++i) {
        const char *path = files[i];

        int fd_in = open(path, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "archiver: cannot open input file '%s': %s\n", path, strerror(errno));
            exit_code = 1;
            continue;
        }

        struct stat st;
        if (fstat(fd_in, &st) < 0) {
            fprintf(stderr, "archiver: cannot stat '%s': %s\n", path, strerror(errno));
            close(fd_in);
            exit_code = 1;
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            fprintf(stderr, "archiver: '%s' is not a regular file, skipping\n", path);
            close(fd_in);
            exit_code = 1;
            continue;
        }

        size_t name_len = strlen(path);
        if (name_len > MAX_NAME_LEN) {
            fprintf(stderr, "archiver: file name too long '%s'\n", path);
            close(fd_in);
            exit_code = 1;
            continue;
        }

        if (lseek(fd_arch, 0, SEEK_END) == (off_t)-1) {
            fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
            close(fd_in);
            exit_code = 1;
            break;
        }

        struct FileHeaderDisk hdr;
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.name, path, name_len + 1);
        hdr.size   = (uint64_t)st.st_size;
        hdr.mode   = (uint32_t)st.st_mode;
        hdr.uid    = (uint32_t)st.st_uid;
        hdr.gid    = (uint32_t)st.st_gid;
        hdr.atime  = (int64_t)st.st_atime;
        hdr.mtime  = (int64_t)st.st_mtime;
        hdr.deleted = 0;

        if (write_full(fd_arch, &hdr, sizeof(hdr)) < 0) {
            fprintf(stderr, "archiver: cannot write header for '%s': %s\n", path, strerror(errno));
            close(fd_in);
            exit_code = 1;
            break;
        }

        off_t left = st.st_size;
        while (left > 0) {
            ssize_t n = read(fd_in, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "archiver: read error from '%s': %s\n", path, strerror(errno));
                exit_code = 1;
                break;
            }
            if (n == 0) {
                fprintf(stderr, "archiver: unexpected EOF on '%s'\n", path);
                exit_code = 1;
                break;
            }
            if (write_full(fd_arch, buf, (size_t)n) < 0) {
                fprintf(stderr, "archiver: write error to archive '%s': %s\n", arch_name, strerror(errno));
                exit_code = 1;
                break;
            }
            left -= (off_t)n;
        }

        close(fd_in);

        if (exit_code != 0) break;

        printf("Добавлен файл '%s' (%lld байт)\n", path, (long long)st.st_size);
    }

    close(fd_arch);
    return exit_code;
}

static int do_stat(const char *arch_name) {
    int fd_arch;
    if (open_archive(arch_name, 0, &fd_arch) < 0) {
        return 1;
    }

    if (lseek(fd_arch, ARCH_MAGIC_LEN, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
        close(fd_arch);
        return 1;
    }

    struct FileHeaderDisk hdr;
    int index = 0;

    printf("Содержимое архива '%s':\n", arch_name);

    while (1) {
        int r = read_full_exact(fd_arch, &hdr, sizeof(hdr));
        if (r == 1) break;
        if (r == 2) {
            fprintf(stderr, "archiver: corrupted archive '%s' (truncated header)\n", arch_name);
            close(fd_arch);
            return 1;
        }
        if (r < 0) {
            fprintf(stderr, "archiver: read error '%s': %s\n", arch_name, strerror(errno));
            close(fd_arch);
            return 1;
        }

        // пропускаем данные
        if (lseek(fd_arch, (off_t)hdr.size, SEEK_CUR) == (off_t)-1) {
            fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
            close(fd_arch);
            return 1;
        }

        if (hdr.deleted) continue; // после компактации обычно не будет

        index++;
        printf("  #%d: %s  size=%llu  mode=%o  uid=%u  gid=%u  atime=%lld  mtime=%lld\n",
               index,
               hdr.name,
               (unsigned long long)hdr.size,
               (unsigned)hdr.mode & 0777,
               (unsigned)hdr.uid,
               (unsigned)hdr.gid,
               (long long)hdr.atime,
               (long long)hdr.mtime);
    }

    close(fd_arch);
    return 0;
}

static int extract_one_no_delete(int fd_arch, const char *arch_name, const char *filename) {
    if (lseek(fd_arch, ARCH_MAGIC_LEN, SEEK_SET) == (off_t)-1) {
        fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
        return 1;
    }

    struct FileHeaderDisk hdr;
    int found = 0;

    while (1) {
        int r = read_full_exact(fd_arch, &hdr, sizeof(hdr));
        if (r == 1) break;
        if (r == 2) {
            fprintf(stderr, "archiver: corrupted archive '%s' (truncated header)\n", arch_name);
            return 1;
        }
        if (r < 0) {
            fprintf(stderr, "archiver: read error '%s': %s\n", arch_name, strerror(errno));
            return 1;
        }

        off_t data_off = lseek(fd_arch, 0, SEEK_CUR);
        if (data_off == (off_t)-1) {
            fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
            return 1;
        }

        if (!hdr.deleted && strcmp(hdr.name, filename) == 0) {
            found = 1;

            int fd_out = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd_out < 0) {
                fprintf(stderr, "archiver: cannot create output file '%s': %s\n", filename, strerror(errno));
                return 1;
            }

            if (lseek(fd_arch, data_off, SEEK_SET) == (off_t)-1) {
                fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
                close(fd_out);
                return 1;
            }

            char buf[4096];
            uint64_t left = hdr.size;

            while (left > 0) {
                size_t chunk = (left > sizeof(buf)) ? sizeof(buf) : (size_t)left;
                ssize_t n = read(fd_arch, buf, chunk);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    fprintf(stderr, "archiver: read error from archive '%s': %s\n", arch_name, strerror(errno));
                    close(fd_out);
                    return 1;
                }
                if (n == 0) {
                    fprintf(stderr, "archiver: corrupted archive '%s' (truncated data)\n", arch_name);
                    close(fd_out);
                    return 1;
                }
                if (write_full(fd_out, buf, (size_t)n) < 0) {
                    fprintf(stderr, "archiver: write error to '%s': %s\n", filename, strerror(errno));
                    close(fd_out);
                    return 1;
                }
                left -= (uint64_t)n;
            }

            // восстановление атрибутов
            if (fchmod(fd_out, (mode_t)hdr.mode) < 0) {
                fprintf(stderr, "archiver: fchmod('%s') failed: %s\n", filename, strerror(errno));
            }
            if (fchown(fd_out, (uid_t)hdr.uid, (gid_t)hdr.gid) < 0) {
                // без root может не сработать — не делаем фатальным
            }

            struct timespec ts[2];
            ts[0].tv_sec = hdr.atime; ts[0].tv_nsec = 0;
            ts[1].tv_sec = hdr.mtime; ts[1].tv_nsec = 0;

            if (futimens(fd_out, ts) < 0) {
                fprintf(stderr, "archiver: futimens('%s') failed: %s\n", filename, strerror(errno));
            }

            close(fd_out);

            printf("Извлечён файл '%s'\n", filename);
            return 0;
        }

        // не тот файл — пропускаем данные
        if (lseek(fd_arch, (off_t)hdr.size, SEEK_CUR) == (off_t)-1) {
            fprintf(stderr, "archiver: lseek failed: %s\n", strerror(errno));
            return 1;
        }
    }

    if (!found) {
        fprintf(stderr, "archiver: file '%s' not found in archive '%s'\n", filename, arch_name);
        return 1;
    }
    return 0;
}

static int do_extract(const char *arch_name, int argc, char **files) {
    // 1) сначала извлекаем
    int fd_arch;
    if (open_archive(arch_name, 0, &fd_arch) < 0) {
        return 1;
    }

    int exit_code = 0;
    for (int i = 0; i < argc; ++i) {
        if (extract_one_no_delete(fd_arch, arch_name, files[i]) != 0) {
            exit_code = 1;
        }
    }
    close(fd_arch);

    // 2) затем физически удаляем из архива (компактация)
    // удаляем только те имена, которые просили через -e
    if (compact_archive_remove(arch_name, argc, files) != 0) {
        return 1;
    }

    return exit_code;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    const char *arch_name = argv[1];
    if (argc < 3) {
        fprintf(stderr, "archiver: missing operation for archive '%s'\n", arch_name);
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    const char *op = argv[2];

    if (strcmp(op, "-s") == 0 || strcmp(op, "--stat") == 0) {
        return do_stat(arch_name);
    }

    if (strcmp(op, "-i") == 0 || strcmp(op, "--input") == 0) {
        if (argc < 4) {
            fprintf(stderr, "archiver: no input files specified\n");
            return EXIT_FAILURE;
        }
        return do_input(arch_name, argc - 3, &argv[3]);
    }

    if (strcmp(op, "-e") == 0 || strcmp(op, "--extract") == 0) {
        if (argc < 4) {
            fprintf(stderr, "archiver: no files to extract specified\n");
            return EXIT_FAILURE;
        }
        return do_extract(arch_name, argc - 3, &argv[3]);
    }

    fprintf(stderr, "archiver: unknown operation '%s'\n", op);
    print_help(argv[0]);
    return EXIT_FAILURE;
}
