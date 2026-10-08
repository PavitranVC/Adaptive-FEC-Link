/*
 * testlib.h - a tiny, dependency-free unit-test helper.
 *
 * Usage:
 *   TEST(name) { CHECK(cond); CHECK_EQ_INT(a, b); }
 *   int main(void) { RUN(name); return TEST_REPORT(); }
 */
#ifndef TESTLIB_H
#define TESTLIB_H

#include <stdio.h>
#include <stdlib.h>

static int tl_failures = 0;
static int tl_checks = 0;

#define TEST(name) static void name(void)

#define RUN(name)                                   \
    do {                                            \
        int before_ = tl_failures;                  \
        name();                                     \
        printf("  %-48s %s\n", #name,               \
               tl_failures == before_ ? "ok" : "FAILED"); \
    } while (0)

#define CHECK(cond)                                                     \
    do {                                                                \
        tl_checks++;                                                    \
        if (!(cond)) {                                                  \
            tl_failures++;                                              \
            fprintf(stderr, "    %s:%d: CHECK failed: %s\n",            \
                    __FILE__, __LINE__, #cond);                         \
        }                                                               \
    } while (0)

#define CHECK_EQ_INT(a, b)                                              \
    do {                                                                \
        long long a_ = (long long)(a), b_ = (long long)(b);             \
        tl_checks++;                                                    \
        if (a_ != b_) {                                                 \
            tl_failures++;                                              \
            fprintf(stderr, "    %s:%d: %s == %s failed (%lld vs %lld)\n", \
                    __FILE__, __LINE__, #a, #b, a_, b_);                \
        }                                                               \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                           \
    do {                                                                \
        double a_ = (double)(a), b_ = (double)(b), t_ = (double)(tol);  \
        tl_checks++;                                                    \
        if (!(a_ - b_ <= t_ && b_ - a_ <= t_)) {                        \
            tl_failures++;                                              \
            fprintf(stderr, "    %s:%d: |%s - %s| <= %s failed (%g vs %g)\n", \
                    __FILE__, __LINE__, #a, #b, #tol, a_, b_);          \
        }                                                               \
    } while (0)

#define TEST_REPORT()                                                   \
    (printf("  %d checks, %d failures\n", tl_checks, tl_failures),      \
     tl_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE)

#endif /* TESTLIB_H */
