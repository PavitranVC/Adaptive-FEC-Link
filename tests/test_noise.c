/* Tests for the channel noise models and the profile presets.
 * Statistical checks use ~5 standard deviations of tolerance so they never flake,
 * and fixed seeds so they are reproducible anyway. */
#include <math.h>
#include <string.h>
#include "testlib.h"
#include "bits.h"
#include "noise.h"
#include "profiles.h"

#define NBITS 1000000

static uint8_t buf[NBITS];
static uint8_t mask[NBITS];

/* ---- binary symmetric channel ---- */

TEST(bsc_flip_rate_matches_p) {
    const double ps[3] = {0.001, 0.01, 0.1};
    for (int i = 0; i < 3; i++) {
        noise_params_t np = noise_bsc(ps[i]);
        noise_t ch;
        noise_init(&ch, &np, 123);
        memset(buf, 0, NBITS);
        size_t flips = noise_apply(&ch, buf, NBITS, mask);
        double sigma = sqrt(ps[i] * (1 - ps[i]) / NBITS);
        CHECK_NEAR((double)flips / NBITS, ps[i], 5 * sigma);
        CHECK_EQ_INT(flips, bits_weight(buf, NBITS)); /* buffer really flipped */
    }
}

TEST(bsc_extremes) {
    noise_t ch;
    noise_params_t zero = noise_bsc(0.0), one = noise_bsc(1.0);
    memset(buf, 0, 1000);
    noise_init(&ch, &zero, 1);
    CHECK_EQ_INT(noise_apply(&ch, buf, 1000, NULL), 0);
    noise_init(&ch, &one, 1);
    CHECK_EQ_INT(noise_apply(&ch, buf, 1000, NULL), 1000);
}

TEST(mask_marks_exactly_the_flipped_bits) {
    noise_params_t np = noise_bsc(0.2);
    noise_t ch;
    noise_init(&ch, &np, 5);
    uint8_t orig[500], data[500], m[500];
    for (int i = 0; i < 500; i++) orig[i] = data[i] = (uint8_t)(i % 3 == 0);
    noise_apply(&ch, data, 500, m);
    int ok = 1;
    for (int i = 0; i < 500; i++) ok &= (orig[i] ^ m[i]) == data[i];
    CHECK(ok);
}

TEST(same_seed_same_errors) {
    noise_params_t np = noise_ge(0.01, 0.1, 0.001, 0.5);
    noise_t a, b;
    uint8_t ma[4000], mb[4000], da[4000] = {0}, db[4000] = {0};
    noise_init(&a, &np, 77); noise_init(&b, &np, 77);
    noise_apply(&a, da, 4000, ma); noise_apply(&b, db, 4000, mb);
    CHECK(memcmp(ma, mb, 4000) == 0);
}

/* ---- Gilbert-Elliott ---- */

/* With e_good = 0 and e_bad = 1 the flips ARE the bad state, so we can measure
 * (a) the fraction of time in the bad state = p_gb / (p_gb + p_bg) and
 * (b) the mean length of a bad run (a burst) = 1 / p_bg  (geometric dwell time). */
TEST(ge_state_occupancy_and_mean_burst_length) {
    const double p_gb = 0.01, p_bg = 0.2;
    noise_params_t np = noise_ge(p_gb, p_bg, 0.0, 1.0);
    noise_t ch;
    noise_init(&ch, &np, 2024);
    memset(buf, 0, NBITS);
    size_t flips = noise_apply(&ch, buf, NBITS, mask);
    double pi_bad = p_gb / (p_gb + p_bg);
    CHECK_NEAR((double)flips / NBITS, pi_bad, 0.05 * pi_bad);
    size_t runs = 0;
    for (size_t i = 0; i < NBITS; i++)
        if (mask[i] && (i == 0 || !mask[i - 1])) runs++;
    double mean_run = (double)flips / (double)runs;
    CHECK_NEAR(mean_run, 1.0 / p_bg, 0.05 / p_bg);
    CHECK_NEAR(noise_expected_ber(&np), pi_bad, 1e-12);
}

TEST(ge_average_ber_matches_stationary_formula) {
    noise_params_t np = noise_ge(0.005, 0.08, 0.002, 0.5);
    noise_t ch;
    noise_init(&ch, &np, 99);
    memset(buf, 0, NBITS);
    size_t flips = noise_apply(&ch, buf, NBITS, NULL);
    double want = noise_expected_ber(&np);
    CHECK_NEAR((double)flips / NBITS, want, 0.08 * want);
}

TEST(ge_state_persists_across_calls) {
    /* Applying the model to 1000 chunks of 1000 bits must give the same errors as one call
     * of 10^6 bits: the channel has memory between frames. */
    noise_params_t np = noise_ge(0.01, 0.1, 0.0, 1.0);
    noise_t a, b;
    noise_init(&a, &np, 3); noise_init(&b, &np, 3);
    static uint8_t whole[100000], chunked[100000];
    memset(whole, 0, sizeof whole); memset(chunked, 0, sizeof chunked);
    noise_apply(&a, whole, 100000, NULL);
    for (int i = 0; i < 100; i++) noise_apply(&b, chunked + 1000 * i, 1000, NULL);
    CHECK(memcmp(whole, chunked, sizeof whole) == 0);
}

/* ---- fixed-length burst ---- */

TEST(burst_has_exact_span_inside_frame) {
    noise_params_t np = noise_burst(10, 1.0, 0.5);
    noise_t ch;
    noise_init(&ch, &np, 8);
    int bad = 0;
    for (int f = 0; f < 5000; f++) {
        uint8_t d[200] = {0}, m[200];
        noise_apply(&ch, d, 200, m);
        int first = -1, last = -1;
        for (int i = 0; i < 200; i++)
            if (m[i]) { if (first < 0) first = i; last = i; }
        if (first < 0 || last - first + 1 != 10) bad++; /* first and last bit of burst flipped */
    }
    CHECK_EQ_INT(bad, 0);
}

TEST(burst_occurrence_and_density) {
    const double p_frame = 0.3, density = 0.5;
    const int L = 12, frames = 20000;
    noise_params_t np = noise_burst(L, p_frame, density);
    noise_t ch;
    noise_init(&ch, &np, 31);
    int hit = 0;
    size_t flips = 0;
    for (int f = 0; f < frames; f++) {
        uint8_t d[256] = {0};
        size_t k = noise_apply(&ch, d, 256, NULL);
        if (k) hit++;
        flips += k;
    }
    double sigma = sqrt(p_frame * (1 - p_frame) / frames);
    CHECK_NEAR((double)hit / frames, p_frame, 5 * sigma);
    /* first+last always flipped, inner L-2 bits with probability density */
    double mean_flips = 2.0 + (L - 2) * density;
    CHECK_NEAR((double)flips / hit, mean_flips, 0.05 * mean_flips);
}

TEST(burst_longer_than_frame_is_clamped) {
    noise_params_t np = noise_burst(500, 1.0, 1.0);
    noise_t ch;
    noise_init(&ch, &np, 4);
    uint8_t d[100] = {0};
    CHECK_EQ_INT(noise_apply(&ch, d, 100, NULL), 100);
}

/* ---- profiles ---- */

TEST(profiles_lookup_and_sanity) {
    const profile_t *t = profile_by_name("toll");
    const profile_t *h = profile_by_name("hospital");
    CHECK(t != NULL && h != NULL);
    CHECK(profile_by_name("nope") == NULL);
    CHECK(t == &PROFILE_TOLL_PLAZA);
    CHECK(profile_by_id(profile_id(t)) == t && profile_by_id(profile_id(h)) == h);
    CHECK(profile_by_id(7) == NULL);
    CHECK(h == &PROFILE_HOSPITAL_IMAGING);
    const profile_t *ps[2] = {t, h};
    for (int i = 0; i < 2; i++) {
        const profile_t *p = ps[i];
        CHECK(p->bsc_p >= 0 && p->bsc_p <= 1);
        CHECK(p->ge.p_gb > 0 && p->ge.p_gb < 1 && p->ge.p_bg > 0 && p->ge.p_bg < 1);
        CHECK(p->ge.e_good < p->ge.e_bad);
        CHECK(p->burst.length >= 1);
    }
    /* hospital interference is burstier: longer bad dwell, longer fixed bursts */
    CHECK(1.0 / h->ge.p_bg > 1.0 / t->ge.p_bg);
    CHECK(h->burst.length > t->burst.length);
}

TEST(profile_noise_override_of_p) {
    const profile_t *t = &PROFILE_TOLL_PLAZA;
    noise_params_t a = profile_noise(t, NOISE_BSC, -1.0);
    CHECK_NEAR(a.bsc.p, t->bsc_p, 0);
    noise_params_t b = profile_noise(t, NOISE_BSC, 0.05);
    CHECK_NEAR(b.bsc.p, 0.05, 0);
    noise_params_t c = profile_noise(t, NOISE_GE, 0.02);       /* --p = p_gb for GE */
    CHECK_NEAR(c.ge.p_gb, 0.02, 0);
    CHECK_NEAR(c.ge.p_bg, t->ge.p_bg, 0);
    noise_params_t d = profile_noise(t, NOISE_BURST, 0.7);     /* --p = burst prob per frame */
    CHECK_NEAR(d.burst.p_frame, 0.7, 0);
    CHECK_EQ_INT(d.burst.length, t->burst.length);
}

TEST(model_names) {
    noise_model_t m;
    CHECK(noise_model_from_name("bsc", &m) == 0 && m == NOISE_BSC);
    CHECK(noise_model_from_name("ge", &m) == 0 && m == NOISE_GE);
    CHECK(noise_model_from_name("burst", &m) == 0 && m == NOISE_BURST);
    CHECK(noise_model_from_name("x", &m) != 0);
    CHECK(strcmp(noise_model_name(NOISE_GE), "ge") == 0);
}

int main(void) {
    RUN(bsc_flip_rate_matches_p);
    RUN(bsc_extremes);
    RUN(mask_marks_exactly_the_flipped_bits);
    RUN(same_seed_same_errors);
    RUN(ge_state_occupancy_and_mean_burst_length);
    RUN(ge_average_ber_matches_stationary_formula);
    RUN(ge_state_persists_across_calls);
    RUN(burst_has_exact_span_inside_frame);
    RUN(burst_occurrence_and_density);
    RUN(burst_longer_than_frame_is_clamped);
    RUN(profiles_lookup_and_sanity);
    RUN(profile_noise_override_of_p);
    RUN(model_names);
    return TEST_REPORT();
}
