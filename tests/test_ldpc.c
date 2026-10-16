/* Tests for the (3,6)-regular LDPC code and the Gallager bit-flipping decoder (include/ldpc.h).
 * Written before src/ldpc.c (overnight run C). */
#include <string.h>
#include "testlib.h"
#include "ldpc.h"
#include "rng.h"

static void random_bits(rng_t *r, uint8_t *b, int n) {
    for (int i = 0; i < n; i++) b[i] = (uint8_t)(rng_next(r) & 1u);
}

static const ldpc_code_t *code(void) { return ldpc_default(); }

TEST(default_code_builds) { CHECK(ldpc_default() != NULL); }

/* ---- structure of H ---- */

TEST(regular_3_6) {
    const ldpc_code_t *c = code();
    int rowdeg[LDPC_M] = {0};
    for (int i = 0; i < LDPC_N; i++)
        for (int e = 0; e < LDPC_WC; e++) {
            int j = c->col[i][e];
            CHECK(j >= 0 && j < LDPC_M);
            rowdeg[j]++;
            for (int f = 0; f < e; f++) CHECK(c->col[i][f] != j);   /* no double edges */
            int found = 0;                                         /* row view agrees */
            for (int g = 0; g < LDPC_WR; g++) found += c->row[j][g] == i;
            CHECK_EQ_INT(found, 1);
        }
    for (int j = 0; j < LDPC_M; j++) CHECK_EQ_INT(rowdeg[j], LDPC_WR);
}

/* A 4-cycle = two bits that share two (or more) checks. There must be none. */
TEST(no_4_cycles) {
    const ldpc_code_t *c = code();
    int worst = 0;
    for (int a = 0; a < LDPC_N; a++)
        for (int b = a + 1; b < LDPC_N; b++) {
            int shared = 0;
            for (int e = 0; e < LDPC_WC; e++)
                for (int f = 0; f < LDPC_WC; f++) shared += c->col[a][e] == c->col[b][f];
            if (shared > worst) worst = shared;
        }
    CHECK(worst <= 1);
}

/* Independent brute-force 6-cycle search: bits a, b, d and checks x != y != z with
 * a-x-b, b-y-d, d-z-a. Compared against ldpc_girth(). */
static int has_6_cycle(const ldpc_code_t *c) {
    for (int a = 0; a < LDPC_N; a++)
        for (int e = 0; e < LDPC_WC; e++) {
            int x = c->col[a][e];
            for (int g = 0; g < LDPC_WR; g++) {
                int b = c->row[x][g];
                if (b == a) continue;
                for (int f = 0; f < LDPC_WC; f++) {
                    int y = c->col[b][f];
                    if (y == x) continue;
                    for (int h = 0; h < LDPC_WR; h++) {
                        int d = c->row[y][h];
                        if (d == a || d == b) continue;
                        for (int u = 0; u < LDPC_WC; u++)
                            for (int v = 0; v < LDPC_WC; v++) {
                                int z = c->col[d][u];
                                if (z != x && z != y && z == c->col[a][v]) return 1;
                            }
                    }
                }
            }
        }
    return 0;
}

TEST(girth_is_reported_correctly) {
    const ldpc_code_t *c = code();
    int g = ldpc_girth(c);
    CHECK_EQ_INT(g, c->girth);
    CHECK(g >= 6);
    if (has_6_cycle(c)) CHECK_EQ_INT(g, 6);
    else CHECK(g >= 8);
}

TEST(build_is_deterministic) {
    static ldpc_code_t a, b;   /* static: ~50 kB each */
    CHECK_EQ_INT(ldpc_build(&a, 7), 0);
    CHECK_EQ_INT(ldpc_build(&b, 7), 0);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    CHECK(memcmp(&a, code(), sizeof a) != 0);  /* another seed gives another H */
}

/* info and parity positions partition 0..n-1 */
TEST(positions_partition) {
    const ldpc_code_t *c = code();
    int seen[LDPC_N] = {0};
    for (int i = 0; i < LDPC_K; i++) seen[c->info_pos[i]]++;
    for (int j = 0; j < LDPC_M; j++) seen[c->par_pos[j]]++;
    for (int i = 0; i < LDPC_N; i++) CHECK_EQ_INT(seen[i], 1);
}

/* ---- encoder ---- */

/* Every unit-vector codeword (a row of the generator G) has zero syndrome. Since the code is
 * linear, every codeword is a XOR of such rows, so ALL 2^128 codewords have zero syndrome. */
TEST(generator_rows_have_zero_syndrome) {
    const ldpc_code_t *c = code();
    for (int i = 0; i < LDPC_K; i++) {
        uint8_t d[LDPC_K] = {0}, w[LDPC_N];
        d[i] = 1;
        ldpc_encode(c, d, w);
        CHECK_EQ_INT(ldpc_syndrome(c, w, NULL), 0);
    }
}

TEST(random_codewords_zero_syndrome_and_systematic) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 11);
    for (int t = 0; t < 2000; t++) {
        uint8_t d[LDPC_K], w[LDPC_N], s[LDPC_M];
        random_bits(&r, d, LDPC_K);
        ldpc_encode(c, d, w);
        CHECK_EQ_INT(ldpc_syndrome(c, w, s), 0);
        for (int i = 0; i < LDPC_K; i++) CHECK(w[c->info_pos[i]] == d[i]);
    }
}

TEST(encoder_is_linear) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 12);
    for (int t = 0; t < 500; t++) {
        uint8_t a[LDPC_K], b[LDPC_K], ab[LDPC_K], wa[LDPC_N], wb[LDPC_N], wab[LDPC_N];
        random_bits(&r, a, LDPC_K);
        random_bits(&r, b, LDPC_K);
        for (int i = 0; i < LDPC_K; i++) ab[i] = a[i] ^ b[i];
        ldpc_encode(c, a, wa); ldpc_encode(c, b, wb); ldpc_encode(c, ab, wab);
        for (int i = 0; i < LDPC_N; i++) CHECK(wab[i] == (wa[i] ^ wb[i]));
    }
}

/* ---- decoder ---- */

TEST(clean_word_decodes_without_changes) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 13);
    uint8_t d[LDPC_K], w[LDPC_N], out[LDPC_K];
    random_bits(&r, d, LDPC_K);
    ldpc_encode(c, d, w);
    int it = -1;
    CHECK_EQ_INT(ldpc_decode_iter(c, w, out, LDPC_MAX_ITER, &it), 0);
    CHECK_EQ_INT(it, 0);
    CHECK(memcmp(d, out, LDPC_K) == 0);
}

/* Every single error at every position, on several random codewords. */
TEST(every_single_error_corrected) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 14);
    for (int t = 0; t < 8; t++) {
        uint8_t d[LDPC_K], clean[LDPC_N];
        random_bits(&r, d, LDPC_K);
        ldpc_encode(c, d, clean);
        for (int p = 0; p < LDPC_N; p++) {
            uint8_t w[LDPC_N], out[LDPC_K];
            memcpy(w, clean, LDPC_N);
            w[p] ^= 1;
            CHECK_EQ_INT(ldpc_decode(c, w, out), 1);
            CHECK(memcmp(w, clean, LDPC_N) == 0);
            CHECK(memcmp(out, d, LDPC_K) == 0);
        }
    }
}

/* Every one of the 256*255/2 = 32640 double-error patterns. The decoder only sees the syndrome
 * H e (see ldpc.h), so one random codeword covers all codewords. */
TEST(every_double_error_corrected) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 15);
    uint8_t d[LDPC_K], clean[LDPC_N];
    random_bits(&r, d, LDPC_K);
    ldpc_encode(c, d, clean);
    long bad = 0;
    for (int p = 0; p < LDPC_N; p++)
        for (int q = p + 1; q < LDPC_N; q++) {
            uint8_t w[LDPC_N], out[LDPC_K];
            memcpy(w, clean, LDPC_N);
            w[p] ^= 1; w[q] ^= 1;
            if (ldpc_decode(c, w, out) != 2 || memcmp(w, clean, LDPC_N) != 0) bad++;
        }
    CHECK_EQ_INT(bad, 0);
}

/* Heavy random error patterns: whatever happens, a reported success is ALWAYS a codeword
 * (zero syndrome) and the data bits are read from it; a non-codeword is always a failure. */
TEST(never_returns_non_codeword_as_success) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 16);
    long ok = 0, fail = 0;
    for (int t = 0; t < 20000; t++) {
        uint8_t d[LDPC_K], w[LDPC_N], out[LDPC_K];
        random_bits(&r, d, LDPC_K);
        ldpc_encode(c, d, w);
        int weight = 3 + (int)rng_below(&r, 40);
        for (int e = 0; e < weight; e++) w[rng_below(&r, LDPC_N)] ^= 1;
        int ret = ldpc_decode(c, w, out);
        int syn = ldpc_syndrome(c, w, NULL);
        if (ret >= 0) { ok++; CHECK_EQ_INT(syn, 0); }
        else { fail++; CHECK(syn > 0); }
        for (int i = 0; i < LDPC_K; i++) CHECK(out[i] == w[c->info_pos[i]]);
    }
    CHECK(ok > 0 && fail > 0);    /* both outcomes really happen in this range */
}

/* A completely random word is (almost surely) far from every codeword: detected failure. */
TEST(random_word_is_detected_failure) {
    const ldpc_code_t *c = code();
    rng_t r;
    rng_seed(&r, 17);
    int failures = 0;
    for (int t = 0; t < 200; t++) {
        uint8_t w[LDPC_N], out[LDPC_K];
        random_bits(&r, w, LDPC_N);
        int it = 0;
        if (ldpc_decode_iter(c, w, out, LDPC_MAX_ITER, &it) < 0) {
            failures++;
            CHECK(it <= LDPC_MAX_ITER);
        }
    }
    CHECK(failures >= 190);
}

int main(void) {
    RUN(default_code_builds);
    RUN(regular_3_6);
    RUN(no_4_cycles);
    RUN(girth_is_reported_correctly);
    RUN(build_is_deterministic);
    RUN(positions_partition);
    RUN(generator_rows_have_zero_syndrome);
    RUN(random_codewords_zero_syndrome_and_systematic);
    RUN(encoder_is_linear);
    RUN(clean_word_decodes_without_changes);
    RUN(every_single_error_corrected);
    RUN(every_double_error_corrected);
    RUN(never_returns_non_codeword_as_success);
    RUN(random_word_is_detected_failure);
    return TEST_REPORT();
}
