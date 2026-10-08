/* Tests for shortened Reed-Solomon over GF(256). */
#include <string.h>
#include "testlib.h"
#include "rng.h"
#include "rs.h"

static void random_bytes(rng_t *r, uint8_t *b, int n) {
    for (int i = 0; i < n; i++) b[i] = (uint8_t)(rng_next(r) >> 56);
}

TEST(parameters_and_generator_roots) {
    for (int t = 1; t <= 16; t++) {
        const rs_code_t *c = rs_get(16, t);
        CHECK(c != NULL);
        CHECK_EQ_INT(c->k, 16);
        CHECK_EQ_INT(c->n, 16 + 2 * t);
        CHECK_EQ_INT(c->nroots, 2 * t);
        /* g(alpha^j) = 0 for j = 1..2t */
        for (int j = 1; j <= 2 * t; j++) {
            int acc = 0;
            for (int d = c->nroots; d >= 0; d--) acc = gf_mul(&c->gf, acc, gf_exp(&c->gf, j)) ^ c->g[d];
            CHECK_EQ_INT(acc, 0);
        }
    }
    CHECK(rs_get(16, 0) == NULL);
    CHECK(rs_get(250, 4) == NULL);   /* n would exceed 255 */
}

TEST(encode_is_systematic_and_valid) {
    const rs_code_t *c = rs_get(16, 4);
    rng_t r;
    rng_seed(&r, 1);
    uint8_t d[16], cw[24], S[8];
    for (int trial = 0; trial < 100; trial++) {
        random_bytes(&r, d, 16);
        rs_encode(c, d, cw);
        CHECK(memcmp(cw + 8, d, 16) == 0);
        CHECK(rs_syndromes(c, cw, S));
    }
}

/* Any <= t symbol errors with arbitrary values are corrected (random positions and values). */
TEST(corrects_up_to_t_symbol_errors_randomized) {
    rng_t r;
    rng_seed(&r, 2);
    long bad = 0;
    for (int t = 1; t <= 8; t++) {
        const rs_code_t *c = rs_get(16, t);
        for (int trial = 0; trial < 3000; trial++) {
            uint8_t d[16], cw[48], rx[48], out[16];
            random_bytes(&r, d, 16);
            rs_encode(c, d, cw);
            memcpy(rx, cw, (size_t)c->n);
            int e = (int)rng_below(&r, (uint32_t)t + 1), made = 0;
            while (made < e) {
                int pos = (int)rng_below(&r, (uint32_t)c->n);
                if (rx[pos] != cw[pos]) continue;
                rx[pos] ^= (uint8_t)(1 + rng_below(&r, 255));
                made++;
            }
            int ret = rs_decode(c, rx, out);
            if (ret != e || memcmp(out, d, 16) || memcmp(rx, cw, (size_t)c->n)) bad++;
        }
    }
    CHECK_EQ_INT(bad, 0);
}

/* Exhaustive over positions for t = 4: every pair of symbol positions with fixed values. */
TEST(t4_all_double_symbol_positions) {
    const rs_code_t *c = rs_get(16, 4);
    uint8_t d[16], cw[24], rx[24], out[16];
    for (int i = 0; i < 16; i++) d[i] = (uint8_t)(i * 17 + 3);
    rs_encode(c, d, cw);
    int bad = 0;
    for (int a = 0; a < 24; a++)
        for (int b = a + 1; b < 24; b++) {
            memcpy(rx, cw, 24);
            rx[a] ^= 0xA5; rx[b] ^= 0x01;
            if (rs_decode(c, rx, out) != 2 || memcmp(out, d, 16)) bad++;
        }
    CHECK_EQ_INT(bad, 0);
}

TEST(too_many_errors_fail_or_give_a_codeword) {
    const rs_code_t *c = rs_get(16, 4);
    rng_t r;
    rng_seed(&r, 3);
    int garbage = 0, detected = 0;
    for (int trial = 0; trial < 3000; trial++) {
        uint8_t d[16], cw[24], rx[24], out[16], S[8];
        random_bytes(&r, d, 16);
        rs_encode(c, d, cw);
        memcpy(rx, cw, 24);
        for (int e = 0; e < 6; e++) rx[rng_below(&r, 24)] ^= (uint8_t)(1 + rng_below(&r, 255));
        int ret = rs_decode(c, rx, out);
        if (ret < 0) detected++;
        else if (!rs_syndromes(c, rx, S)) garbage++;
    }
    CHECK_EQ_INT(garbage, 0);
    CHECK(detected > 2500);   /* shortened codes detect almost every overload */
}

/* Burst correction: a burst of L bits touches at most ceil((L + 7) / 8) bytes, so t = 4
 * corrects EVERY bit burst of length <= 8 * (t - 1) + 1 = 25, at every position. */
TEST(t4_corrects_every_bit_burst_up_to_25) {
    const rs_code_t *c = rs_get(16, 4);
    uint8_t d[16], cw[24], rx[24], out[16];
    for (int i = 0; i < 16; i++) d[i] = (uint8_t)(0xC3 ^ (i * 29));
    rs_encode(c, d, cw);
    int bad = 0;
    for (int L = 1; L <= 25; L++)
        for (int start = 0; start + L <= 24 * 8; start++) {
            memcpy(rx, cw, 24);
            for (int b = start; b < start + L; b++) rx[b / 8] ^= (uint8_t)(0x80 >> (b % 8));
            if (rs_decode(c, rx, out) < 0 || memcmp(out, d, 16)) bad++;
        }
    CHECK_EQ_INT(bad, 0);
}

int main(void) {
    RUN(parameters_and_generator_roots);
    RUN(encode_is_systematic_and_valid);
    RUN(corrects_up_to_t_symbol_errors_randomized);
    RUN(t4_all_double_symbol_positions);
    RUN(too_many_errors_fail_or_give_a_codeword);
    RUN(t4_corrects_every_bit_burst_up_to_25);
    return TEST_REPORT();
}
