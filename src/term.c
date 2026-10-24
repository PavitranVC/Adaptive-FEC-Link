/*
 * term.c - ANSI colour switch and the "[TAG] message" line printer shared by all executables.
 * Output goes to stdout only, which is also the stream tested for colour support.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "term.h"

/* Colours are off until term_init() runs. */
static int g_color = 0;

void term_init(int flag) {
    if (flag >= 0) { g_color = flag; return; }   /* explicit --color / --no-color wins */
    const char *t = getenv("TERM");
    g_color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL && !(t && !strcmp(t, "dumb"));
}

int term_enabled(void) { return g_color; }

const char *term_c(term_color_t c) {
    /* Index = term_color_t value: this table must stay in the same order as the enum.
     * The two-parameter entries (41;97m etc.) set background and foreground together. */
    static const char *const CODES[] = {
        "\033[0m", "\033[1m", "\033[2m", "\033[31m", "\033[32m", "\033[33m", "\033[34m",
        "\033[35m", "\033[36m", "\033[41;97m", "\033[42;30m", "\033[45;97m"};
    /* Rejects an out-of-range colour (returns "") instead of reading past the table. */
    if ((unsigned)c >= sizeof CODES / sizeof CODES[0]) return "";
    return g_color ? CODES[c] : "";
}

void term_say(const char *tag, term_color_t color, const char *fmt, ...) {
    va_list ap;
    printf("%s%s[%s]%s ", term_c(T_BOLD), term_c(color), tag, term_c(T_RESET));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
