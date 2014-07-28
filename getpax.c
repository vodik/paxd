#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <fts.h>
#include <elf.h>
#include <sys/xattr.h>

#include "flags.h"

#define _noreturn_ __attribute__((noreturn))

static int is_elf(const char *fpath)
{
    char header[4];
    int fd = open(fpath, O_RDONLY);
    read(fd, header, sizeof(header));
    close(fd);
    return memcmp(header, ELFMAG, SELFMAG) == 0;
}

static int print_flags(const char *fpath, bool filter)
{
    char flags[PAX_LEN + 1];
    ssize_t len = get_pax_flags(flags, sizeof(flags), fpath);
    if (len < 0) {
        if (errno != ENODATA) {
            warn("failed to get pax for '%s'", fpath);
            return -1;
        } else if (filter) {
            return 0;
        }

        len = 0;
    }

    flags[len] = 0;

    char output[PAX_LEN + 1];
    size_t i;
    for (i = 0; i < PAX_LEN; ++i) {
        char flag = PAX_FLAGS[i];
        output[i] = strchr(flags, flag) ? flag : '-';
    }
    output[i] = 0;

    printf("%s %s\n", output, fpath);
    return 0;
}

static int walk_filesystem(char *const *path_argv, bool filter)
{
    FTS *fts = NULL;
    FTSENT *child, *parent = NULL;

    fts = fts_open(path_argv, FTS_COMFOLLOW | FTS_NOCHDIR, NULL);
    if (fts != NULL) {
        while ((parent = fts_read(fts)) != NULL) {
            child = fts_children(fts, 0);
            if (errno != 0) {
                warn("ftw_children");
                continue;
            }

            while (child && child->fts_link) {
                char *fpath;

                child = child->fts_link;

                if (child->fts_info != FTS_F)
                    continue;

                asprintf(&fpath, "%s%s", child->fts_path, child->fts_name);
                if (!is_elf(fpath))
                    continue;

                print_flags(fpath, filter);
                free(fpath);
            }
        }
    }

    fts_close(fts);
    return 0;
}

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help            display this help and exit\n"
        " -v, --version         display version\n"
        " -R, --recursive       recursive show flags\n"
        " -f, --filter          only show files with set flags\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    bool recusive = false, filter = false;
    int rc = 0;

    static const struct option opts[] = {
        { "help", no_argument, 0, 'h' },
        { "recusive", no_argument, 0, 'R' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(argc, argv, "hRf", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
        case 'R':
            recusive = true;
            break;
        case 'f':
            filter = true;
            break;
        default:
            usage(stderr);
        }
    }

    argc -= optind;
    argv += optind;

    if (recusive) {
        rc = walk_filesystem(argv, filter);
    } else {
        int i;

        for (i = 0; i < argc; ++i) {
            const char *fpath = argv[i];

            if (!is_elf(fpath)) {
                warnx("'%s' is not an elf binary", fpath);
                rc = 1;
            } else {
                rc |= print_flags(argv[i], false) < 0 ? 1 : 0;
            }
        }
    }

    return rc;
}
