#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "term.h"

static int g_color = 0;

void term_init(int flag) {
    if (flag >= 0) { g_color = flag; return; }
    const char *t = getenv("TERM");
    g_color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL && !(t && !strcmp(t, "dumb"));
}

int term_enabled(void) { return g_color; }

const char *term_c(term_color_t c) {
    static const char *const CODES[] = {
        "\033[0m", "\033[1m", "\033[2m", "\033[31m", "\033[32m", "\033[33m", "\033[34m",
        "\033[35m", "\033[36m", "\033[41;97m", "\033[42;30m", "\033[45;97m"};
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
