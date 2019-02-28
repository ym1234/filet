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
#include <libgen.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define ENT_ALLOC_NUM 64

struct direlement {
    enum {
        TYPE_DIR,
        TYPE_SYML,
        TYPE_SYML_TO_DIR,
        TYPE_EXEC,
        TYPE_NORM,
    } type;

    const char *d_name;
};

static struct termios g_old_termios;
static FILE *devfile;
static int g_row;
static int g_col;

static void
tprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(devfile, format, args);
    va_end(args);
}

/**
 * Got too used to rust. This falls back to fallback, if name isn't set
 */
static const char *
getenv_or(const char *name, const char *fallback)
{
    const char *res = getenv(name);
    if (!res) {
        return fallback;
    }
    return res;
}

/**
 * Comparison function for direlements
 */
static int
direlemcmp(const void *va, const void *vb)
{
    const struct direlement *a = va;
    const struct direlement *b = vb;

    bool a_is_dir = a->type == TYPE_DIR || a->type == TYPE_SYML_TO_DIR;
    bool b_is_dir = b->type == TYPE_DIR || b->type == TYPE_SYML_TO_DIR;

    if (a_is_dir != b_is_dir) {
        return a_is_dir ? -1 : 1;
    }

    return strcmp(a->d_name, b->d_name);
}

/**
 * Sets the terminal size on g_row
 */
static bool
get_term_size(void)
{
    struct winsize wsize;
    if (ioctl(fileno(devfile), TIOCGWINSZ, &wsize) < 0) {
        perror("ioctl");
        return false;
    }

    g_row = wsize.ws_row;
    g_col = wsize.ws_col;

    return true;
}

/**
 * Used as SIGWINCH (terminal resize handler)
 */
static void
handle_winch(int sig)
{
    signal(sig, SIG_IGN);
    get_term_size();
    signal(sig, handle_winch);
}

/**
 * Resets the terminal to it's prior state
 */
static void
restore_terminal(void)
{
    if (tcsetattr(fileno(devfile), TCSAFLUSH, &g_old_termios) < 0) {
        perror("tcsetattr");
    }

    tprintf(
        "\033[?25h"   // unhide cursor
        "\033[;r"     // reset scroll region
        "\033[?1049l" // restore main screen
    );
}

/**
 * Sets up the terminal for TUI use (read every char, differentiate \r and \n,
 * don't echo, hide the cursor, fix a scroll region, switch to a second screen)
 */
static bool
setup_terminal(void)
{
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return false;
    }
    if (tcgetattr(fd, &g_old_termios) < 0) {
        perror("tcgetattr");
        return false;
    }

    struct termios raw = g_old_termios;
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ICANON);

    // Reading block until there is at least one byte, similar to
    // setvbuf(stdout, NULL, _IOFBF, 0);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &raw) < 0) {
        perror("tcsetattr");
        return false;
    }

    devfile = fdopen(fd, "r+");

    tprintf(
        "\033[?1049h" // use alternative screen buffer
        "\033[?7l"    // disable line wrapping
        "\033[?25l"   // hide cursor
        "\033[2J"     // clear screen
        "\033[3;%dr", // limit scrolling to scrolling area
        g_row);

    return true;
}

/**
 * Read a directory into ents.
 *
 * Returns the number of elements in the dir.
 */
static size_t
read_dir(
    const char *path,
    struct direlement **ents,
    size_t *ents_size,
    DIR **last_dir,
    bool show_hidden)
{
    size_t n = 0;
    DIR *dir;
    if (*last_dir) {
        closedir(*last_dir);
    }
    dir       = opendir(path);
    *last_dir = dir;
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            const char *name = ent->d_name;
            int fd           = dirfd(dir);
            struct stat sb;

            if (name[0] == '.' &&
                (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
                continue;
            }

            if (!show_hidden && name[0] == '.') {
                continue;
            }

            if (fstatat(fd, name, &sb, AT_SYMLINK_NOFOLLOW) < 0) {
                continue;
            }

            if (n == *ents_size) {
                *ents_size += ENT_ALLOC_NUM;
                struct direlement *tmp =
                    realloc(*ents, *ents_size * sizeof(*tmp));
                if (!tmp) {
                    perror("realloc");
                    exit(EXIT_FAILURE);
                }
                *ents = tmp;
            }

            (*ents)[n].d_name = ent->d_name;

            if (S_ISDIR(sb.st_mode)) {
                (*ents)[n].type = TYPE_DIR;
            } else if (S_ISLNK(sb.st_mode)) {
                if (!(fstatat(dirfd(dir), (*ents)[n].d_name, &sb, 0) < 0 ||
                      !S_ISDIR(sb.st_mode))) {
                    (*ents)[n].type = TYPE_SYML_TO_DIR;
                } else {
                    (*ents)[n].type = TYPE_SYML;
                }
            } else {
                if (sb.st_mode & S_IXUSR) {
                    (*ents)[n].type = TYPE_EXEC;
                } else {
                    (*ents)[n].type = TYPE_NORM;
                }
            }

            ++n;
        }
        qsort(*ents, n, sizeof(**ents), direlemcmp);
    }

    return n;
}

/**
 * Spawns a new process, waits for it and returns
 */
static void
spawn(const char *path, const char *cmd, const char *argv1)
{
    int status;
    pid_t pid = fork();

    if (pid < 0) {
        return;
    }

    restore_terminal();
    fflush(devfile);

    if (pid == 0) {
        // Set the stdout of the program to /dev/tty
        dup2(STDOUT_FILENO, fileno(devfile));
        if (chdir(path) < 0) {
            _exit(EXIT_FAILURE);
        }
        execlp(cmd, cmd, argv1, NULL);
        // NOTREACHED
        _exit(EXIT_FAILURE);
    } else {
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    setup_terminal();
}

/**
 * Draws a single directory entry in it's own line
 *
 * Assumes the cursor is at the beginning of the line
 */
static void
draw_line(const struct direlement *ent, bool is_sel)
{
    switch (ent->type) {
    case TYPE_DIR:
        tprintf("\033[34;1m");
        break;
    case TYPE_SYML: // FALLTHROUGH
    case TYPE_SYML_TO_DIR:
        tprintf("\033[36;1m");
        break;
    case TYPE_EXEC:
        tprintf("\033[32;1m");
        break;
    case TYPE_NORM:
        tprintf("\033[0m");
        break;
    }

    if (is_sel) {
        tprintf(">  %s", ent->d_name);
    } else {
        tprintf(
            "  %s ",
            ent->d_name); // space to clear the last char on unindenting it
    }
}

/**
 * Redraws the whole screen. Avoid this if possible
 */
static void
redraw(
    const struct direlement *ents,
    const char *user_and_hostname,
    const char *path,
    size_t n,
    size_t sel,
    size_t offset)
{
    // clear screen and redraw status
    fprintf(
        devfile,
        "\033[2J"       // clear screen
        "\033[H"        // go to 0,0
        "%s"            // print username@hostname
        "\033[34;1m%s"  // print path
        " \033[0m[%zu]" // number of entries
        "\r\n",         // enter scrolling region
        user_and_hostname,
        path,
        n);

    if (n == 0) {
        tprintf("\033[31;7mdirectory empty\033[27m");
    } else {
        for (size_t i = offset; i < n && i - offset < (size_t)g_row - 2; ++i) {
            tprintf("\n");
            draw_line(&ents[i], i == sel);
            tprintf("\r");
        }
    }

    // move cursor to selection
    tprintf("\033[3;1H");
}

int
main(int argc, char **argv)
{
    /* doesn't matter anymore i think, we always just open /dev/tty */
    /* if (!(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) { */
    /*     fprintf(stderr, "isatty: not connected to a tty\n"); */
    /*     exit(EXIT_FAILURE); */
    /* } */

    char *path = malloc(PATH_MAX);
    if (!path) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    if (argc > 1) {
        if (!realpath(argv[1], path)) {
            perror("realpath");
            exit(EXIT_FAILURE);
        }
    } else {
        if (!getcwd(path, PATH_MAX)) {
            perror("getcwd");
            exit(EXIT_FAILURE);
        }
    }

    const char *editor = getenv_or("EDITOR", "vi");
    const char *shell  = getenv_or("SHELL", "/bin/sh");
    const char *home   = getenv_or("HOME", "/");
    const char *user   = getlogin();

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
    atexit(restore_terminal);

    if (!get_term_size()) {
        exit(EXIT_FAILURE);
    }

    if (signal(SIGWINCH, handle_winch) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    char *user_and_hostname = malloc(
        strlen(user) + strlen(hostname) + strlen("\033[32;1m@\033[0m:") + 1);
    if (!user_and_hostname) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    strcpy(user_and_hostname, "\033[32;1m");
    strcat(user_and_hostname, user);
    strcat(user_and_hostname, "@");
    strcat(user_and_hostname, hostname);
    strcat(user_and_hostname, "\033[0m:");

    bool show_hidden = false;
    bool fetch_dir   = true;
    size_t sel       = 0;
    size_t y         = 0;
    DIR *last_dir    = NULL;
    size_t n;

    for (;;) {
        if (fetch_dir) {
            fetch_dir = false;
            sel       = 0;
            y         = 0;
            n = read_dir(path, &ents, &ents_size, &last_dir, show_hidden);

            redraw(ents, user_and_hostname, path, n, sel, 0);
        }

        fflush(devfile);

        int c = fgetc(devfile);

        switch (c) {
        case 'h':
            dirname(path);
            fetch_dir = true;
            break;
        case '~':
            strcpy(path, home);
            fetch_dir = true;
            break;
        case '/':
            strcpy(path, "/");
            fetch_dir = true;
            break;
        case '.':
            show_hidden = !show_hidden;
            fetch_dir   = true;
            break;
        case 'r':
            fetch_dir = true;
            break;
        case 's':
            spawn(path, shell, NULL);
            fetch_dir = true;
            break;
        case 'q': {
            printf("%s\n", path);
            fflush(stdout);
            exit(EXIT_SUCCESS);
            break;
        }
        }

        if (n == 0) {
            continue; // rest of the commands require at least one entry
        }

        switch (c) {
        case 'j':
            if (sel < n - 1) {
                draw_line(&ents[sel], false);
                tprintf("\r\n");
                ++sel;
                draw_line(&ents[sel], true);
                tprintf("\r");

                if (y < (size_t)g_row - 3) {
                    ++y;
                }
            }
            break;
        case 'k':
            if (sel > 0) {
                draw_line(&ents[sel], false);
                if (y == 0) {
                    tprintf("\r\033[L");
                } else {
                    tprintf("\r\033[A");
                    --y;
                }
                --sel;
                draw_line(&ents[sel], true);
                tprintf("\r");
            }
            break;
        case 'l':
            if (ents[sel].type == TYPE_DIR ||
                ents[sel].type == TYPE_SYML_TO_DIR) {
                // don't append to /
                if (path[1] != '\0') {
                    strcat(path, "/");
                }
                strcat(path, ents[sel].d_name);
                fetch_dir = true;
            }
            break;
        case 'g':
            if (sel - y == 0) {
                draw_line(&ents[sel], false);
                tprintf("\033[3;1H");
                sel = 0;
                draw_line(&ents[sel], true);
                tprintf("\r");
            } else {
                // screen needs to be redrawn
                sel = 0;
                y   = 0;
                redraw(ents, user_and_hostname, path, n, sel, 0);
            }
            break;
        case 'G':
            if (sel + g_row - 2 - y >= n) {
                draw_line(&ents[sel], false);
                fprintf(
                    devfile,
                    "\033[%lu;1H",
                    2 + (n < ((size_t)g_row - 3) ? n : (size_t)g_row));
                sel = n - 1;
                y   = g_row - 3;
                draw_line(&ents[sel], true);
                tprintf("\r");
            } else {
                // screen needs to be redrawn
                sel = n - 1;
                y   = g_row - 3;
                redraw(ents, user_and_hostname, path, n, sel, n - (g_row - 2));
                tprintf("\033[%d;1H", g_row);
            }
            break;
        case 'e':
            spawn(path, editor, ents[sel].d_name);
            fetch_dir = true;
            break;
        case 'x': {
            int fd = dirfd(last_dir);
            unlinkat(
                fd,
                ents[sel].d_name,
                ents[sel].type == TYPE_DIR ? AT_REMOVEDIR : 0);
            fetch_dir = true;
            break;
        }
        }
    }
}
