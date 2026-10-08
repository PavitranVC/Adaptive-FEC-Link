/* Tests for binary BCH codes: BCH(15,7) t=2 (exhaustive) and BCH(31,16) t=3. */
#include <string.h>
#include "testlib.h"
#include "bch.h"
#include "rng.h"

static void int_to_bits(unsigned long v, int k, uint8_t *bits) {
    for (int i = 0; i < k; i++) bits[i] = (uint8_t)((v >> i) & 1u);
}

static int is_codeword(const bch_code_t *c, const uint8_t *word) {
    uint8_t S[2 * BCH_MAX_T];
    return bch_syndromes(c, word, S);
}

TEST(parameters) {
    const bch_code_t *a = bch157(), *b = bch3116();
    CHECK_EQ_INT(a->n, 15); CHECK_EQ_INT(a->k, 7); CHECK_EQ_INT(a->t, 2);
    CHECK_EQ_INT(b->n, 31); CHECK_EQ_INT(b->k, 16); CHECK_EQ_INT(b->t, 3);
}

TEST(bch157_generator_is_textbook) {
    /* g(x) = (x^4+x+1)(x^4+x^3+x^2+x+1) = x^8+x^7+x^6+x^4+1 */
    const bch_code_t *c = bch157();
    const uint8_t want[9] = {1, 0, 0, 0, 1, 0, 1, 1, 1};
    CHECK_EQ_INT(c->deg_g, 8);
    CHECK(memcmp(c->g, want, 9) == 0);
}

/* g(x) must vanish at alpha^1..alpha^2t (that is what makes it a BCH code). */
TEST(generator_roots) {
    const bch_code_t *codes[2] = {bch157(), bch3116()};
    for (int i = 0; i < 2; i++) {
        const bch_code_t *c = codes[i];
        for (int j = 1; j <= 2 * c->t; j++) {
            int acc = 0;
            for (int d = 0; d <= c->deg_g; d++)
                if (c->g[d]) acc ^= gf_exp(&c->gf, d * j);
            CHECK_EQ_INT(acc, 0);
        }
    }
}

/* Linear code: minimum distance = minimum weight of a non-zero codeword. */
static int min_weight(const bch_code_t *c) {
    uint8_t d[64], w[64];
    int best = 1000;
    for (unsigned long v = 1; v < (1ul << c->k); v++) {
        int_to_bits(v, c->k, d);
        bch_encode(c, d, w);
        int wt = 0;
        for (int i = 0; i < c->n; i++) wt += w[i];
        if (wt < best) best = wt;
    }
    return best;
}

TEST(bch157_min_distance_5) { CHECK_EQ_INT(min_weight(bch157()), 5); }
TEST(bch3116_min_distance_7) { CHECK_EQ_INT(min_weight(bch3116()), 7); }

TEST(encoding_is_systematic_and_valid) {
    const bch_code_t *c = bch157();
    uint8_t d[7], w[15];
    for (unsigned v = 0; v < 128; v++) {
        int_to_bits(v, 7, d);
        bch_encode(c, d, w);
        CHECK(memcmp(w + (c->n - c->k), d, 7) == 0); /* data sits in the top k positions */
        CHECK(is_codeword(c, w));
    }
}

TEST(berlekamp_massey_single_error) {
    /* one error at position p: S_j = alpha^(p*j), Lambda(x) = 1 + alpha^p x */
    const bch_code_t *c = bch157();
    for (int p = 0; p < 15; p++) {
        uint8_t S[4], lambda[5];
        for (int j = 0; j < 4; j++) S[j] = (uint8_t)gf_exp(&c->gf, p * (j + 1));
        int L = bch_berlekamp_massey(&c->gf, S, 4, lambda);
        CHECK_EQ_INT(L, 1);
        CHECK_EQ_INT(lambda[0], 1);
        CHECK_EQ_INT(lambda[1], gf_exp(&c->gf, p));
    }
}

/* EXHAUSTIVE: every data word x every error pattern of weight 0, 1 and 2.
 * Pattern = {i, j} with i in -1..14 and j in i+1..15, where -1 / 15 mean "no error". */
TEST(bch157_corrects_every_pattern_up_to_weight_2) {
    const bch_code_t *c = bch157();
    uint8_t d[7], w[15], r[15], out[7];
    long bad = 0, cases = 0;
    for (unsigned v = 0; v < 128; v++) {
        int_to_bits(v, 7, d);
        bch_encode(c, d, w);
        for (int i = -1; i < 15; i++)
            for (int j = i + 1; j <= 15; j++) {
                if (i == -1 && j != 15) continue;      /* singles are covered by (i, 15) */
                memcpy(r, w, 15);
                int weight = 0;
                if (i >= 0) { r[i] ^= 1; weight++; }
                if (j < 15) { r[j] ^= 1; weight++; }
                cases++;
                int ret = bch_decode(c, r, out);
                if (ret != weight || memcmp(out, d, 7) || memcmp(r, w, 15)) bad++;
            }
    }
    CHECK_EQ_INT(bad, 0);
    CHECK_EQ_INT(cases, 128L * (1 + 15 + 105));
}

/* Beyond t: the decoder must either report failure or output a VALID codeword (never garbage). */
TEST(bch157_weight3_fails_or_outputs_a_codeword) {
    const bch_code_t *c = bch157();
    uint8_t d[7] = {1, 0, 1, 1, 0, 0, 1}, w[15], r[15], out[7];
    bch_encode(c, d, w);
    int detected = 0, miscorrected = 0, garbage = 0;
    for (int a = 0; a < 15; a++)
        for (int b = a + 1; b < 15; b++)
            for (int e = b + 1; e < 15; e++) {
                memcpy(r, w, 15);
                r[a] ^= 1; r[b] ^= 1; r[e] ^= 1;
                int ret = bch_decode(c, r, out);
                if (ret < 0) detected++;
                else if (!is_codeword(c, r)) garbage++;
                else miscorrected++;
            }
    CHECK_EQ_INT(garbage, 0);
    CHECK_EQ_INT(detected + miscorrected, 455);
    CHECK(detected > 0);
}

TEST(bch3116_exhaustive_up_to_weight_3_for_sample_words) {
    const bch_code_t *c = bch3116();
    uint8_t d[16], w[31], r[31], out[16];
    const unsigned long samples[3] = {0x0000, 0xBEEF, 0xFFFF};
    long bad = 0;
    for (int s = 0; s < 3; s++) {
        int_to_bits(samples[s], 16, d);
        bch_encode(c, d, w);
        for (int a = -1; a < 31; a++)
            for (int b = a + 1; b < 31; b++)
                for (int e = b + 1; e < 32; e++) {   /* e == 31 means "no third error" */
                    memcpy(r, w, 31);
                    if (a >= 0) r[a] ^= 1;
                    r[b] ^= 1;
                    if (e < 31) r[e] ^= 1;
                    if (bch_decode(c, r, out) < 0 || memcmp(out, d, 16)) bad++;
                }
    }
    CHECK_EQ_INT(bad, 0);
}

TEST(bch3116_randomized_200k) {
    const bch_code_t *c = bch3116();
    rng_t rng;
    rng_seed(&rng, 31);
    uint8_t d[16], w[31], r[31], out[16];
    long bad = 0;
    for (long trial = 0; trial < 200000; trial++) {
        for (int i = 0; i < 16; i++) d[i] = (uint8_t)(rng_next(&rng) & 1);
        bch_encode(c, d, w);
        memcpy(r, w, 31);
        int weight = (int)rng_below(&rng, 4);           /* 0..3 errors */
        int flipped = 0;
        while (flipped < weight) {
            int pos = (int)rng_below(&rng, 31);
            if (r[pos] == w[pos]) { r[pos] ^= 1; flipped++; }
        }
        int ret = bch_decode(c, r, out);
        if (ret != weight || memcmp(out, d, 16) || memcmp(r, w, 31)) bad++;
    }
    CHECK_EQ_INT(bad, 0);
}

int main(void) {
    RUN(parameters);
    RUN(bch157_generator_is_textbook);
    RUN(generator_roots);
    RUN(bch157_min_distance_5);
    RUN(bch3116_min_distance_7);
    RUN(encoding_is_systematic_and_valid);
    RUN(berlekamp_massey_single_error);
    RUN(bch157_corrects_every_pattern_up_to_weight_2);
    RUN(bch157_weight3_fails_or_outputs_a_codeword);
    RUN(bch3116_exhaustive_up_to_weight_3_for_sample_words);
    RUN(bch3116_randomized_200k);
    return TEST_REPORT();
}
