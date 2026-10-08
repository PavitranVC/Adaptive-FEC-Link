/* Tests for the in-process simulator used by bin/bench. */
#include "testlib.h"
#include "sim.h"

TEST(clean_channel_is_always_correct) {
    for (int i = 0; i < fec_code_count(); i++) {
        fec_config_t c = fec_config(fec_code_at(i)->id);
        noise_params_t np = noise_bsc(0.0);
        sim_result_t r;
        sim_run(&c, &np, 300, 1, &r);
        CHECK_EQ_INT(r.frames, 300);
        CHECK_EQ_INT(r.correct, 300);
        CHECK_EQ_INT(r.detected_fail + r.silent_wrong, 0);
        CHECK(r.mean_decode_us >= 0.0);
    }
}

TEST(uncoded_fails_whenever_a_bit_flips) {
    fec_config_t c = fec_config(CODE_NONE);
    noise_params_t np = noise_burst(1, 1.0, 0.5);   /* exactly one flip per frame */
    sim_result_t r;
    sim_run(&c, &np, 500, 2, &r);
    CHECK_EQ_INT(r.detected_fail, 500);             /* CRC-32 catches every single flip */
}

TEST(hamming_fixes_any_single_flip_per_frame) {
    fec_config_t c = fec_config(CODE_HAMMING74);
    noise_params_t np = noise_burst(1, 1.0, 0.5);
    sim_result_t r;
    sim_run(&c, &np, 500, 3, &r);
    CHECK_EQ_INT(r.correct, 500);
}

TEST(bch3116_fixes_any_burst_of_3) {
    fec_config_t c = fec_config(CODE_BCH3116);
    noise_params_t np = noise_burst(3, 1.0, 1.0);   /* 3 flips spread over <= 2 blocks */
    sim_result_t r;
    sim_run(&c, &np, 500, 4, &r);
    CHECK_EQ_INT(r.correct, 500);
}

TEST(rates_add_up_and_runs_are_reproducible) {
    fec_config_t c = fec_config(CODE_HAMMING1511);
    noise_params_t np = noise_bsc(0.03);
    sim_result_t a, b;
    sim_run(&c, &np, 2000, 9, &a);
    sim_run(&c, &np, 2000, 9, &b);
    CHECK_EQ_INT(a.correct + a.detected_fail + a.silent_wrong, 2000);
    CHECK_EQ_INT(a.correct, b.correct);
    CHECK_EQ_INT(a.detected_fail, b.detected_fail);
    CHECK_EQ_INT(a.flips, b.flips);
    CHECK(a.correct < 2000 && a.correct > 0);
    CHECK_NEAR(a.code_rate, 128.0 / 180.0, 1e-9);
}

TEST(stronger_code_wins_on_random_errors) {
    noise_params_t np = noise_bsc(0.01);
    fec_config_t h = fec_config(CODE_HAMMING74), b = fec_config(CODE_BCH3116);
    sim_result_t rh, rb;
    sim_run(&h, &np, 3000, 5, &rh);
    sim_run(&b, &np, 3000, 5, &rb);
    CHECK(rb.correct > rh.correct);
}

int main(void) {
    RUN(clean_channel_is_always_correct);
    RUN(uncoded_fails_whenever_a_bit_flips);
    RUN(hamming_fixes_any_single_flip_per_frame);
    RUN(bch3116_fixes_any_burst_of_3);
    RUN(rates_add_up_and_runs_are_reproducible);
    RUN(stronger_code_wins_on_random_errors);
    return TEST_REPORT();
}
