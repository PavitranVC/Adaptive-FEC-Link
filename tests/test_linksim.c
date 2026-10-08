/* Tests for the in-process link simulator with retransmission strategies (fec / arq / harq). */
#include "testlib.h"
#include "linksim.h"

static linksim_params_t base(strategy_t s, code_id_t code, double p) {
    linksim_params_t lp;
    linksim_defaults(&lp);
    lp.strategy = s;
    lp.fixed = fec_config(code);
    lp.frames = 3000;
    lp.seed = 11;
    lp.nseg = 1;
    lp.seg[0].noise = noise_bsc(p);
    lp.seg[0].frames = 0;               /* 0 = forever */
    return lp;
}

TEST(fec_has_no_retransmissions_and_constant_latency) {
    linksim_params_t lp = base(STRAT_FEC, CODE_HAMMING74, 0.01);
    linksim_result_t r;
    linksim_run(&lp, &r, NULL);
    CHECK_EQ_INT(r.frames, 3000);
    CHECK_EQ_INT(r.transmissions, 3000);
    CHECK_NEAR(r.success_rate, 0.935, 0.02);       /* matches the plain FEC benchmark */
    CHECK_NEAR(r.p99_latency_ms, r.mean_latency_ms, 0.01);
}

TEST(arq_recovers_by_retransmitting) {
    linksim_params_t lp = base(STRAT_ARQ, CODE_HAMMING74, 0.002);
    lp.max_retries = 10;
    linksim_result_t r;
    linksim_run(&lp, &r, NULL);
    CHECK(r.success_rate > 0.99);                 /* uncoded success 0.77 -> ARQ fixes it */
    CHECK(r.retx_per_frame > 0.2 && r.retx_per_frame < 0.4); /* ~ 1/0.77 - 1 = 0.30 */
    CHECK(r.p99_latency_ms > r.mean_latency_ms);  /* retransmissions create a latency tail */
    CHECK(r.mean_latency_ms > lp.rtt_ms / 2);
}

TEST(harq_beats_fec_and_needs_fewer_retx_than_arq) {
    linksim_params_t f = base(STRAT_FEC, CODE_HAMMING74, 0.01);
    linksim_params_t h = base(STRAT_HARQ, CODE_HAMMING74, 0.01);
    linksim_params_t a = base(STRAT_ARQ, CODE_HAMMING74, 0.01);
    linksim_result_t rf, rh, ra;
    linksim_run(&f, &rf, NULL);
    linksim_run(&h, &rh, NULL);
    linksim_run(&a, &ra, NULL);
    CHECK(rh.success_rate > rf.success_rate);
    CHECK(rh.success_rate > 0.999);
    CHECK(rh.retx_per_frame < ra.retx_per_frame / 10);
}

TEST(max_retries_bounds_transmissions) {
    linksim_params_t lp = base(STRAT_ARQ, CODE_NONE, 0.2);   /* hopeless channel */
    lp.max_retries = 3;
    linksim_result_t r;
    linksim_run(&lp, &r, NULL);
    CHECK_EQ_INT(r.transmissions, 3000L * 4);
    CHECK(r.success_rate < 0.01);
}

TEST(lost_feedback_costs_timeouts_but_not_correctness) {
    linksim_params_t lp = base(STRAT_HARQ, CODE_BCH157, 0.01);
    lp.fb_drop = 0.3;
    linksim_result_t r;
    linksim_run(&lp, &r, NULL);
    CHECK(r.success_rate > 0.99);
    CHECK(r.timeouts > 500);                  /* ~30% of first feedbacks lost */
    CHECK(r.mean_latency_ms < lp.timeout_ms); /* the frame was delivered before the timeout */
}

TEST(reproducible) {
    linksim_params_t lp = base(STRAT_HARQ, CODE_HAMMING74, 0.02);
    linksim_result_t a, b;
    linksim_run(&lp, &a, NULL);
    linksim_run(&lp, &b, NULL);
    CHECK_EQ_INT(a.transmissions, b.transmissions);
    CHECK_NEAR(a.success_rate, b.success_rate, 0);
}

int main(void) {
    RUN(fec_has_no_retransmissions_and_constant_latency);
    RUN(arq_recovers_by_retransmitting);
    RUN(harq_beats_fec_and_needs_fewer_retx_than_arq);
    RUN(max_retries_bounds_transmissions);
    RUN(lost_feedback_costs_timeouts_but_not_correctness);
    RUN(reproducible);
    return TEST_REPORT();
}
