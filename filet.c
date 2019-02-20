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

#include <termios.h>
#include <unistd.h>

#define WRITE(text) write(STDOUT_FILENO, (text), strlen(text))

#define PROGRAM_NAME "filet"
#define MAX_ITEMS "15"

static struct termios old_termios;

static void
err(const char *fmt, ...)
{
    fprintf(stderr, PROGRAM_NAME ": ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static void
restore_terminal(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios) < 0) {
        perror("tcsetattr");
    }

    const char *cmd =
        "\033[?7h"     // enable line wrapping
        "\033[?25h"    // unhide cursor
        "\033[2J"      // clear terminal
        "\033[;r"      // reset scroll region
        "\033[?1049l"; // restore main screen

    if (WRITE(cmd) < 0) {
        perror("write");
    }
}

static bool
setup_terminal(void)
{
    if (tcgetattr(STDIN_FILENO, &old_termios) < 0) {
        perror("tcgetattr");
        return false;
    }

    atexit(restore_terminal);

    struct termios raw = old_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        perror("tcsetattr");
        return false;
    }

    const char *cmd =
        "\033[?1049h"            // use alternative screen buffer
        "\033[?7l"               // diable line wrapping
        "\033[?25l"              // hide cursor
        "\033[2J"                // clear screen
        "\033[1;" MAX_ITEMS "r"; // limit scrolling to scrolling area

    if (WRITE(cmd) < 0) {
        perror("write");
        return false;
    }

    return true;
}

int
main(void)
{
    if (!(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) {
        err("not connected to a tty");
        exit(EXIT_FAILURE);
    }

    if (!setup_terminal()) {
        exit(EXIT_FAILURE);
    }

    bool is_about_to_quit = false;

    char c;
    while (!is_about_to_quit) {
        if (read(STDIN_FILENO, &c, 1) < 0) {
            break;
        }

        switch (c) {
        case 'q':
            is_about_to_quit = true;
            break;
        }
    }
}
