/* Tests for the code ladder, the receiver's sliding window and the adaptive controller. */
#include <string.h>
#include "testlib.h"
#include "adapt.h"
#include "window.h"

TEST(ladder_is_valid_and_ordered) {
    CHECK_EQ_INT(ladder_levels(), 6);
    CHECK(ladder_config(0).code == CODE_NONE);
    CHECK(ladder_config(1).code == CODE_HAMMING74);
    CHECK(ladder_config(2).code == CODE_BCH157);
    CHECK(ladder_config(3).code == CODE_BCH3116);
    CHECK(ladder_config(4).code == CODE_RS);
    for (int l = 0; l < ladder_levels(); l++) {
        fec_config_t c = ladder_config(l);
        CHECK(fec_config_valid(&c));
        CHECK_EQ_INT(ladder_level_of(&c), l);
        CHECK(strlen(ladder_label(l)) > 0);
    }
    fec_config_t other = fec_config(CODE_SECDED84);
    CHECK_EQ_INT(ladder_level_of(&other), -1);
    /* every level is at least as robust as the one below on a random-error channel */
    for (int l = 1; l < ladder_levels(); l++) {
        fec_config_t lo = ladder_config(l - 1), hi = ladder_config(l);
        if (l == 4) continue;  /* RS(t=4) trades random-error strength for burst strength */
        CHECK(adapt_predict_fail(&hi, 0.01) <= adapt_predict_fail(&lo, 0.01) + 1e-12);
    }
}

TEST(predicted_failure_matches_binomial_maths) {
    fec_config_t h = fec_config(CODE_HAMMING74), n = fec_config(CODE_NONE);
    /* Hamming(7,4): P(block ok) = (1-p)^7 + 7p(1-p)^6; frame = 32 blocks */
    CHECK_NEAR(adapt_predict_fail(&h, 0.01), 0.0628, 0.001);
    /* uncoded: any of 128 bits wrong */
    CHECK_NEAR(adapt_predict_fail(&n, 0.01), 1 - 0.27642, 0.001);
    CHECK_NEAR(adapt_predict_fail(&h, 0.0), 0.0, 1e-15);
    CHECK(adapt_predict_fail(&h, 0.02) > adapt_predict_fail(&h, 0.01));
}

TEST(window_slides_and_resets_on_level_change) {
    window_t w;
    window_init(&w, 4);
    feedback_t f;
    for (int i = 0; i < 6; i++) window_push(&w, 2, i % 2, 10, 100);  /* failures at i=1,3,5 */
    window_fill_feedback(&w, &f);
    CHECK_EQ_INT(f.win_n, 4);
    CHECK_EQ_INT(f.win_fail, 2);              /* last four: i = 2..5 -> fails at 3,5 */
    CHECK_EQ_INT(f.win_corrected, 20);        /* only successful frames (i = 2, 4) count */
    CHECK_EQ_INT(f.win_bits, 200);
    window_push(&w, 3, 0, 5, 50);             /* new level: old evidence is discarded */
    window_fill_feedback(&w, &f);
    CHECK_EQ_INT(f.win_n, 1);
    CHECK_EQ_INT(f.win_corrected, 5);
}

/* Synthetic feedback describing a full window at the controller's current level. */
static feedback_t window_fb(const adapt_t *a, int n, int fails, double ber) {
    feedback_t f;
    memset(&f, 0, sizeof f);
    f.type = FB_ACK;
    f.level = (uint8_t)a->level;
    f.win_n = (uint16_t)n;
    f.win_fail = (uint16_t)fails;
    f.win_bits = 10000;
    f.win_corrected = (uint32_t)(ber * 10000);
    return f;
}

/* Feeds the same window feedback k times (the controller decides once per W feedbacks). */
static int feed(adapt_t *a, int n, int fails, double ber, int k) {
    int moved = 0;
    for (int i = 0; i < k; i++) {
        feedback_t f = window_fb(a, n, fails, ber);
        moved += adapt_update(a, &f);
    }
    return moved;
}

static adapt_t make(int start) {
    adapt_params_t p;
    adapt_defaults(&p);
    p.start_level = start;
    adapt_t a;
    adapt_init(&a, &p);
    return a;
}

TEST(steps_up_on_failures_only_with_a_full_window) {
    adapt_t a = make(1);
    int W = a.p.window;
    CHECK_EQ_INT(feed(&a, W - 1, W - 1, 0.01, 3 * W), 0);  /* terrible, but window not full */
    CHECK_EQ_INT(feed(&a, W, W / 2, 0.01, 1), +1);         /* 50% failures > up threshold */
    CHECK_EQ_INT(a.level, 2);
    CHECK_EQ_INT(feed(&a, W, W / 2, 0.01, W - 1), 0);      /* new level: needs W fresh frames */
    CHECK_EQ_INT(feed(&a, W, W / 2, 0.01, 1), +1);
    CHECK_EQ_INT(a.level, 3);
    feedback_t f = window_fb(&a, W, W / 2, 0.01);
    f.level = 2;                                           /* stale: describes the old level */
    for (int i = 0; i < 2 * W; i++) CHECK_EQ_INT(adapt_update(&a, &f), 0);
    CHECK_EQ_INT(a.level, 3);
}

TEST(steps_down_only_when_lower_level_is_predicted_safe) {
    adapt_t a = make(5);
    int W = a.p.window;
    /* p = 0.01: RS(t=4) predicted ~3.6% failures -> may step to L4, then to BCH(31,16) and
     * BCH(15,7) (0.8%), but NOT to Hamming(7,4) (6.3% > down threshold 5%) */
    int steps = 0;
    for (int i = 0; i < 40; i++) steps += feed(&a, W, 0, 0.01, W) != 0;
    CHECK_EQ_INT(a.level, 2);
    CHECK_EQ_INT(steps, 3);
}

TEST(no_step_down_while_failures_are_present) {
    adapt_t a = make(3);
    int W = a.p.window;
    /* failure rate between the thresholds (12.5%): hold the level */
    CHECK_EQ_INT(feed(&a, W, W / 8, 0.0005, 20 * W), 0);
    CHECK_EQ_INT(a.level, 3);
}

TEST(failed_probe_doubles_the_hold_time) {
    adapt_t a = make(3);
    int W = a.p.window;
    int guard = 0;
    while (a.level == 3 && guard++ < 100 * W) feed(&a, W, 0, 0.001, 1);
    CHECK_EQ_INT(a.level, 2);                    /* probed down */
    CHECK_EQ_INT(a.hold, 1);
    CHECK_EQ_INT(feed(&a, W, W, 0.001, W), +1);  /* the lower level fails at once */
    CHECK_EQ_INT(a.hold, 2);                     /* next probe only after 2 windows */
    CHECK_EQ_INT(a.failed_probes, 1);
    long frames = 0;
    while (a.level == 3 && frames < 100 * W) { feed(&a, W, 0, 0.001, 1); frames++; }
    CHECK(frames >= 2 * W);
}

TEST(successful_probe_resets_the_hold) {
    adapt_t a = make(3);
    int W = a.p.window;
    a.hold = 4;
    int guard = 0;
    while (a.level == 3 && guard++ < 100 * W) feed(&a, W, 0, 0.0001, 1);
    CHECK_EQ_INT(a.level, 2);
    a.p.min_level = 2;                           /* keep it here for the check */
    feed(&a, W, 0, 0.004, 3 * W);                /* lower level copes for 3 windows */
    CHECK_EQ_INT(a.level, 2);
    CHECK_EQ_INT(a.hold, 1);
}

int main(void) {
    RUN(ladder_is_valid_and_ordered);
    RUN(predicted_failure_matches_binomial_maths);
    RUN(window_slides_and_resets_on_level_change);
    RUN(steps_up_on_failures_only_with_a_full_window);
    RUN(steps_down_only_when_lower_level_is_predicted_safe);
    RUN(no_step_down_while_failures_are_present);
    RUN(failed_probe_doubles_the_hold_time);
    RUN(successful_probe_resets_the_hold);
    return TEST_REPORT();
}
