/* Copyright (c), Niclas Meyer <niclas@countingsort.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_ITEMS "99"
#define ENT_ALLOC_NUM 64

struct direlement {
    enum {
        TYPE_DIR,
        TYPE_SYML,
        TYPE_EXEC,
        TYPE_NORM,
    } type;

    const char *d_name;
};

static struct termios old_termios;

static const char *
getenv_or(const char *name, const char *fallback)
{
    const char *res = getenv(name);
    if (!res) {
        return fallback;
    }
    return res;
}

static int
direntcmp(const void *va, const void *vb)
{
    const struct direlement *a = va;
    const struct direlement *b = vb;

    return strcmp(a->d_name, b->d_name);
}

static void
restore_terminal(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios) < 0) {
        perror("tcsetattr");
    }

    printf(
        "\033[?7h"    // enable line wrapping
        "\033[?25h"   // unhide cursor
        "\033[2J"     // clear terminal
        "\033[;r"     // reset scroll region
        "\033[?1049l" // restore main screen
    );
}

static bool
setup_terminal(void)
{
    setvbuf(stdout, NULL, _IOFBF, 0);

    if (tcgetattr(STDIN_FILENO, &old_termios) < 0) {
        perror("tcgetattr");
        return false;
    }

    atexit(restore_terminal);

    struct termios raw = old_termios;
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ICANON);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        perror("tcsetattr");
        return false;
    }

    printf(
        "\033[?1049h"           // use alternative screen buffer
        "\033[?7l"              // diable line wrapping
        "\033[?25l"             // hide cursor
        "\033[2J"               // clear screen
        "\033[1;" MAX_ITEMS "r" // limit scrolling to scrolling area
    );

    return true;
}

int
main(int argc, char **argv)
{
    if (!(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) {
        fprintf(stderr, "isatty: not connected to a tty");
        exit(EXIT_FAILURE);
    }

    char *path = malloc(PATH_MAX);
    if (!path) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    if (argc > 1) {
        path = strcpy(path, argv[1]);
    } else {
        path = strcpy(path, "/");
    }

    //const char *editor = getenv_or("EDITOR", "vi");
    //const char *home   = getenv_or("HOME", "/");
    //const char *shell = getenv_or("SHELL", "/bin/sh");
    const char *user = getlogin();

    char *hostname = malloc(HOST_NAME_MAX);
    if (!hostname) {
        perror("malloc");
    }

    if (gethostname(hostname, HOST_NAME_MAX) < 0) {
        perror("gethostname");
        free(hostname);
        hostname = NULL;
    }

    size_t ents_size        = ENT_ALLOC_NUM;
    struct direlement *ents = malloc(ents_size * sizeof(*ents));
    if (!ents) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    if (!setup_terminal()) {
        exit(EXIT_FAILURE);
    }

    bool is_about_to_quit = false;
    bool show_hidden      = false;
    bool fetch_dir        = true;
    size_t sel            = 0;
    size_t n;

    while (!is_about_to_quit) {
        if (fetch_dir) {
            n         = 0;
            fetch_dir = false;
            DIR *dir  = opendir(path);
            if (dir) {
                struct dirent *ent;
                while ((ent = readdir(dir))) {
                    const char *name = ent->d_name;
                    int fd           = dirfd(dir);
                    struct stat sb;

                    if (name[0] == '.' &&
                        (name[1] == '\0' ||
                         (name[1] == '.' && name[2] == '\0'))) {
                        continue;
                    }

                    if (!show_hidden && name[0] == '.') {
                        continue;
                    }

                    if (n == ents_size) {
                        ents_size += ENT_ALLOC_NUM;
                        struct direlement *tmp =
                            realloc(ents, ents_size * sizeof(*tmp));
                        if (!tmp) {
                            perror("realloc");
                            exit(EXIT_FAILURE);
                        }
                        ents = tmp;
                    }

                    if (fstatat(fd, name, &sb, AT_SYMLINK_NOFOLLOW) < 0) {
                        continue;
                    }

                    ents[n].d_name = ent->d_name;

                    if (S_ISDIR(sb.st_mode)) {
                        ents[n].type = TYPE_DIR;
                    } else if (S_ISLNK(sb.st_mode)) {
                        ents[n].type = TYPE_SYML;
                    } else {
                        if (sb.st_mode & S_IXUSR) {
                            ents[n].type = TYPE_EXEC;
                        } else {
                            ents[n].type = TYPE_NORM;
                        }
                    }

                    ++n;
                }
                closedir(dir);
                qsort(ents, n, sizeof(*ents), direntcmp);
            }
        }

        printf("\033[2J\033[H");
        printf("\033[32;1m%s", user);
        if (hostname) {
            printf("@%s", hostname);
        }

        printf("\033[0m:\033[34;1m%s\r\n\n", path);

        if (n == 0) {
            printf("\033[31;7mdirectory empty\033[27m");
        }

        for (size_t i = 0; i < n; ++i) {
            switch (ents[i].type) {
            case TYPE_DIR:
                printf("\033[34;1m");
                break;
            case TYPE_SYML:
                printf("\033[36;1m");
                break;
            case TYPE_EXEC:
                printf("\033[32;1m");
                break;
            case TYPE_NORM:
                printf("\033[0m");
                break;
            }

            if (i == sel) {
                printf(">  %s\r\n", ents[i].d_name);
            } else {
                printf("  %s\r\n", ents[i].d_name);
            }
        }

        fflush(stdout);

        switch (getchar()) {
        case 'j':
            if (sel < n - 1) {
                ++sel;
            }
            break;
        case 'k':
            if (sel > 0) {
                --sel;
            }
            break;
        case 'h':
            dirname(path);
            fetch_dir = true;
            break;
        case 'l':
            if (n > 0 && ents[sel].type == TYPE_DIR) {
                strcat(path, "/");
                strcat(path, ents[sel].d_name);
                sel       = 0;
                fetch_dir = true;
            }
            break;
        case '.':
            show_hidden = !show_hidden;
            sel         = 0;
            fetch_dir   = true;
            break;
        case 'r':
            fetch_dir = true;
            break;
        case 'g':
            sel = 0;
            break;
        case 'G':
            sel = n - 1;
            break;
        case 'q':
            is_about_to_quit = true;
            break;
        }
    }
}
