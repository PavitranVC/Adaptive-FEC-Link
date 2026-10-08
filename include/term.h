/*
 * term.h - optional ANSI colours and prefixed output lines ("[VEHICLE] ...").
 * Colours are on when stdout is a terminal, NO_COLOR is unset and TERM != dumb,
 * unless forced with --color / --no-color.
 */
#ifndef TERM_H
#define TERM_H

typedef enum {
    T_RESET, T_BOLD, T_DIM, T_RED, T_GREEN, T_YELLOW, T_BLUE, T_MAGENTA, T_CYAN,
    T_BG_RED, T_BG_GREEN, T_BG_MAGENTA
} term_color_t;

void term_init(int color_flag); /* -1 auto, 0 off, 1 on */
int term_enabled(void);
const char *term_c(term_color_t c); /* escape sequence, or "" when colours are off */

/* Prints "<colour>[TAG]<reset> " + formatted text + newline to stdout. */
void term_say(const char *tag, term_color_t color, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#endif /* TERM_H */
