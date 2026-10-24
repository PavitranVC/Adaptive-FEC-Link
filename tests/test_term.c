/*
 * Tests term.c: the colour table lookup. A failure means term_c() reads outside its table for a
 * bad colour value, or colours leak when switched off.
 */
#include <string.h>
#include "testlib.h"
#include "term.h"

/* Valid colours map to an escape sequence only when colours are on; out-of-range values give "" either way. */
TEST(term_c_bounds) {
    term_init(1);
    CHECK(strcmp(term_c(T_RESET), "\033[0m") == 0);
    CHECK(strlen(term_c(T_BG_MAGENTA)) > 0);
    CHECK(strcmp(term_c((term_color_t)(T_BG_MAGENTA + 1)), "") == 0);
    CHECK(strcmp(term_c((term_color_t)1000), "") == 0);
    CHECK(strcmp(term_c((term_color_t)-1), "") == 0);
    term_init(0);
    CHECK(strcmp(term_c(T_RED), "") == 0);
}

int main(void) {
    RUN(term_c_bounds);
    return TEST_REPORT();
}
