/* Tests for GF(2^m) arithmetic. */
#include "testlib.h"
#include "gf.h"

static int check_field(int m, unsigned poly) {
    gf_t f;
    int bad = 0;
    if (gf_init(&f, m, poly) != 0) return 1000;
    int q = 1 << m, n = q - 1;
    /* alpha generates every non-zero element exactly once, and alpha^n = 1 */
    int seen[256] = {0};
    for (int i = 0; i < n; i++) {
        int e = gf_exp(&f, i);
        if (e == 0 || seen[e]++) bad++;
        if (gf_log(&f, e) != i) bad++;
    }
    if (gf_exp(&f, n) != 1) bad++;
    for (int a = 0; a < q; a++)
        for (int b = 0; b < q; b++) {
            int p = gf_mul(&f, a, b);
            if (p != gf_mul(&f, b, a)) bad++;              /* commutative */
            if ((a == 0 || b == 0) != (p == 0)) bad++;      /* no zero divisors */
            if (b != 0 && gf_div(&f, p, b) != a) bad++;    /* division undoes multiplication */
        }
    for (int a = 1; a < q; a++)
        if (gf_mul(&f, a, gf_inv(&f, a)) != 1) bad++;
    /* distributive law on a sample */
    for (int a = 0; a < q; a += 3)
        for (int b = 0; b < q; b += 5)
            for (int c = 0; c < q; c += 7)
                if (gf_mul(&f, a, b ^ c) != (gf_mul(&f, a, b) ^ gf_mul(&f, a, c))) bad++;
    return bad;
}

TEST(gf16_is_a_field) { CHECK_EQ_INT(check_field(4, 0x13), 0); }
TEST(gf32_is_a_field) { CHECK_EQ_INT(check_field(5, 0x25), 0); }
TEST(gf256_is_a_field) { CHECK_EQ_INT(check_field(8, 0x11D), 0); }

TEST(non_primitive_polynomial_rejected) {
    gf_t f;
    /* x^4+x^3+x^2+x+1 is irreducible but alpha has order 5, not 15 */
    CHECK(gf_init(&f, 4, 0x1F) != 0);
    CHECK(gf_init(&f, 9, 0x211) != 0); /* m out of range */
}

TEST(pow_and_known_values) {
    gf_t f;
    gf_init(&f, 4, 0x13);
    CHECK_EQ_INT(gf_exp(&f, 4), 0x3);         /* alpha^4 = alpha + 1 */
    CHECK_EQ_INT(gf_pow(&f, 2, 4), 0x3);
    CHECK_EQ_INT(gf_pow(&f, 2, -1), gf_inv(&f, 2));
    CHECK_EQ_INT(gf_exp(&f, -1), gf_exp(&f, 14));
}

int main(void) {
    RUN(gf16_is_a_field);
    RUN(gf32_is_a_field);
    RUN(gf256_is_a_field);
    RUN(non_primitive_polynomial_rejected);
    RUN(pow_and_known_values);
    return TEST_REPORT();
}
