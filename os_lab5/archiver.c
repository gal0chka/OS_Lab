#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>     
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#define MAX_NAME_LEN 255
#define ARCH_MAGIC "MYARCH1"
#define ARCH_MAGIC_LEN 7



struct FileHeaderDisk {
    char   name[256];
    uint64_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    int64_t  mtime;
    uint8_t  deleted;
    uint8_t  reserved[7];
};

static void print_help(const char *prog) {
    printf("Примитивный архиватор\n\n");
    printf("Использование:\n");
    printf("  %s -h | --help\n", prog);
    printf("  %s ARCH -i|--input FILE [FILE...]\n", prog);
    printf("  %s ARCH -e|--extract FILE [FILE...]\n", prog);
    printf("  %s ARCH -s|--stat\n", prog);
    printf("\nПримеры:\n");
    printf("  %s myarch.bin -i file1.txt\n", prog);
    printf("  %s myarch.bin -e file1.txt\n", prog);
    printf("  %s myarch.bin -s\n", prog);
}

static int write_full(int fd, const void *buf, size_t count) {
    const uint8_t *p = buf;
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
        p += n;
        left -= (size_t)n;
    }
    return 0;
}

/* Безопасное чтение ровно count байт (для заголовка) */
static int read_full(int fd, void *buf, size_t count) {
    uint8_t *p = buf;
    size_t left = count;
    while (left > 0) {
        ssize_t n = read(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return 1;
        }
        p += n;
        left -= (size_t)n;
    }
    return 0;
}

/* Открытие/создание архива и проверка/запись магической сигнатуры */
static int open_archive(const char *arch_name, int need_rw, int *fd_out) {
    int flags = need_rw ? (O_RDWR | O_CREAT) : O_RDONLY;
    int fd = open(arch_name, flags, 0644);
    if (fd < 0) {
        fprintf(stderr, "archiver: cannot open '%s': %s\n",
                arch_name, strerror(errno));
        return -1;
    }

    char magic[ARCH_MAGIC_LEN];
    ssize_t n = read(fd, magic, ARCH_MAGIC_LEN);
    if (n == 0) {
        // Новый пустой файл — запишем сигнатуру
        if (!need_rw) {
            fprintf(stderr, "archiver: '%s' is empty and read-only\n", arch_name);
            close(fd);
            return -1;
        }
        if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
            perror("lseek");
            close(fd);
            return -1;
        }
        if (write_full(fd, ARCH_MAGIC, ARCH_MAGIC_LEN) < 0) {
            fprintf(stderr, "archiver: cannot write magic to '%s': %s\n",
                    arch_name, strerror(errno));
            close(fd);
            return -1;
        }
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
        // read <0
        fprintf(stderr, "archiver: cannot read '%s': %s\n",
                arch_name, strerror(errno));
        close(fd);
        return -1;
    }

    if (fd_out) *fd_out = fd;
    return 0;
}

/* Добавление файла(ов) в архив */
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
            fprintf(stderr, "archiver: cannot open input file '%s': %s\n",
                    path, strerror(errno));
            exit_code = 1;
            continue;
        }

        struct stat st;
        if (fstat(fd_in, &st) < 0) {
            fprintf(stderr, "archiver: cannot stat '%s': %s\n",
                    path, strerror(errno));
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
            perror("lseek");
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
        hdr.mtime  = (int64_t)st.st_mtime;
        hdr.deleted = 0;

        if (write_full(fd_arch, &hdr, sizeof(hdr)) < 0) {
            fprintf(stderr, "archiver: cannot write header for '%s': %s\n",
                    path, strerror(errno));
            close(fd_in);
            exit_code = 1;
            break;
        }

        // Копирование содержимого
        off_t left = st.st_size;
        while (left > 0) {
            ssize_t n = read(fd_in, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "archiver: read error from '%s': %s\n",
                        path, strerror(errno));
                exit_code = 1;
                break;
            }
            if (n == 0) {
                fprintf(stderr, "archiver: unexpected EOF on '%s'\n", path);
                exit_code = 1;
                break;
            }
            if (write_full(fd_arch, buf, (size_t)n) < 0) {
                fprintf(stderr, "archiver: write error to archive '%s': %s\n",
                        arch_name, strerror(errno));
                exit_code = 1;
                break;
            }
            left -= (off_t)n;
        }

        close(fd_in);

        if (exit_code != 0) {
            // при ошибке в копировании не продолжаем остальные файлы
            break;
        }

        printf("Добавлен файл '%s' (%lld байт)\n", path, (long long)st.st_size);
    }

    close(fd_arch);
    return exit_code;
}

/* Показ содержимого архива (-s) */
static int do_stat(const char *arch_name) {
    int fd_arch;
    if (open_archive(arch_name, 0, &fd_arch) < 0) {
        return 1;
    }

    if (lseek(fd_arch, ARCH_MAGIC_LEN, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        close(fd_arch);
        return 1;
    }

    struct FileHeaderDisk hdr;
    int index = 0;

    printf("Содержимое архива '%s':\n", arch_name);

    while (1) {
        int r = read_full(fd_arch, &hdr, sizeof(hdr));
        if (r == 1) {
            // EOF
            break;
        } else if (r < 0) {
            fprintf(stderr, "archiver: error reading archive: %s\n",
                    strerror(errno));
            close(fd_arch);
            return 1;
        }

        // позиция после заголовка — там данные
        off_t data_off = lseek(fd_arch, 0, SEEK_CUR);
        if (data_off == (off_t)-1) {
            perror("lseek");
            close(fd_arch);
            return 1;
        }

        // Пропускаем данные файла
        if (lseek(fd_arch, (off_t)hdr.size, SEEK_CUR) == (off_t)-1) {
            perror("lseek");
            close(fd_arch);
            return 1;
        }

        index++;

        printf("  #%d: %s  size=%llu  mode=%o  uid=%u  gid=%u  mtime=%lld  %s\n",
               index,
               hdr.name,
               (unsigned long long)hdr.size,
               (unsigned)hdr.mode & 0777,
               (unsigned)hdr.uid,
               (unsigned)hdr.gid,
               (long long)hdr.mtime,
               hdr.deleted ? "[DELETED]" : "");
    }

    close(fd_arch);
    return 0;
}

/* Извлечение файла(ов) (-e) с логическим удалением из архива */
static int extract_one(int fd_arch, const char *arch_name, const char *filename) {
    // Позиция после magic
    if (lseek(fd_arch, ARCH_MAGIC_LEN, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        return 1;
    }

    struct FileHeaderDisk hdr;
    off_t hdr_off = 0;
    off_t data_off = 0;
    int found = 0;

    // Ищем нужный файл
    while (1) {
        hdr_off = lseek(fd_arch, 0, SEEK_CUR);
        if (hdr_off == (off_t)-1) {
            perror("lseek");
            return 1;
        }

        int r = read_full(fd_arch, &hdr, sizeof(hdr));
        if (r == 1) {
            // EOF - не нашли
            break;
        } else if (r < 0) {
            fprintf(stderr, "archiver: error reading archive '%s': %s\n",
                    arch_name, strerror(errno));
            return 1;
        }

        data_off = lseek(fd_arch, 0, SEEK_CUR);
        if (data_off == (off_t)-1) {
            perror("lseek");
            return 1;
        }

        if (!hdr.deleted && strcmp(hdr.name, filename) == 0) {
            found = 1;
            break;
        }

        // иначе пропускаем данные и идём дальше
        if (lseek(fd_arch, (off_t)hdr.size, SEEK_CUR) == (off_t)-1) {
            perror("lseek");
            return 1;
        }
    }

    if (!found) {
        fprintf(stderr, "archiver: file '%s' not found in archive '%s'\n",
                filename, arch_name);
        return 1;
    }

    // Извлекаем в обычный файл
    int fd_out = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd_out < 0) {
        fprintf(stderr, "archiver: cannot create output file '%s': %s\n",
                filename, strerror(errno));
        return 1;
    }

    // Переходим к данным
    if (lseek(fd_arch, data_off, SEEK_SET) == (off_t)-1) {
        perror("lseek");
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
            fprintf(stderr, "archiver: read error from archive '%s': %s\n",
                    arch_name, strerror(errno));
            close(fd_out);
            return 1;
        }
        if (n == 0) {
            fprintf(stderr, "archiver: unexpected EOF while extracting '%s'\n",
                    filename);
            close(fd_out);
            return 1;
        }
        if (write_full(fd_out, buf, (size_t)n) < 0) {
            fprintf(stderr, "archiver: write error to '%s': %s\n",
                    filename, strerror(errno));
            close(fd_out);
            return 1;
        }
        left -= (uint64_t)n;
    }

    // Восстанавливаем права, uid/gid, mtime
    if (fchmod(fd_out, (mode_t)hdr.mode) < 0) {
        fprintf(stderr, "archiver: fchmod('%s') failed: %s\n",
                filename, strerror(errno));
    }

    // chown может не сработать без прав root — не считаем это фатальной ошибкой
    if (fchown(fd_out, (uid_t)hdr.uid, (gid_t)hdr.gid) < 0) {
        // просто предупреждение
        // fprintf(stderr, "archiver: fchown('%s') failed: %s\n", filename, strerror(errno));
    }

    // mtime
    struct timespec ts[2];
    ts[0].tv_sec  = hdr.mtime; ts[0].tv_nsec = 0; // atime
    ts[1].tv_sec  = hdr.mtime; ts[1].tv_nsec = 0; // mtime

    if (futimens(fd_out, ts) < 0) {
        fprintf(stderr, "archiver: futimens('%s') failed: %s\n",
                filename, strerror(errno));
    }

    close(fd_out);

    // Помечаем запись как удалённую в архиве
    hdr.deleted = 1;
    if (lseek(fd_arch, hdr_off, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        return 1;
    }
    if (write_full(fd_arch, &hdr, sizeof(hdr)) < 0) {
        fprintf(stderr, "archiver: cannot mark file '%s' as deleted in archive '%s': %s\n",
                filename, arch_name, strerror(errno));
        return 1;
    }

    printf("Извлечён файл '%s' и помечен удалённым в архиве\n", filename);
    return 0;
}

static int do_extract(const char *arch_name, int argc, char **files) {
    int fd_arch;
    if (open_archive(arch_name, 1, &fd_arch) < 0) {
        return 1;
    }

    int exit_code = 0;
    for (int i = 0; i < argc; ++i) {
        if (extract_one(fd_arch, arch_name, files[i]) != 0) {
            exit_code = 1;
        }
    }

    close(fd_arch);
    return exit_code;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    // ./archiver -h
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    // Иначе argv[1] — это имя архива
    const char *arch_name = argv[1];
    if (argc < 3) {
        fprintf(stderr, "archiver: missing operation for archive '%s'\n", arch_name);
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    const char *op = argv[2];

    if (strcmp(op, "-s") == 0 || strcmp(op, "--stat") == 0) {
        // ./archiver arch_name -s
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
