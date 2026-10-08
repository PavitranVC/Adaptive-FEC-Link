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
    /* no retransmission tail: a tail would add >= one RTT (20 ms). The tolerance only absorbs
     * the real, measured decode time (microseconds, occasionally slower under machine load). */
    CHECK_NEAR(r.p99_latency_ms, r.mean_latency_ms, 1.0);
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

/* ---- adaptive controller, closed loop on a scheduled channel ---- */

#include <stdlib.h>
#include "profiles.h"

static double mean_level(const int *lvl, const int *seg, long from, long to, int want_seg) {
    double sum = 0;
    long n = 0;
    for (long i = from; i < to; i++)
        if (seg[i] == want_seg) { sum += lvl[i]; n++; }
    return n ? sum / n : -1;
}

static long changes(const int *lvl, long from, long to) {
    long c = 0;
    for (long i = from + 1; i < to; i++) c += lvl[i] != lvl[i - 1];
    return c;
}

static linksim_params_t adaptive(long frames, int nseg, const profile_t **profs, long seglen) {
    linksim_params_t lp;
    linksim_defaults(&lp);
    lp.strategy = STRAT_ADAPTIVE;
    lp.frames = frames;
    lp.seed = 21;
    lp.nseg = nseg;
    for (int i = 0; i < nseg; i++) {
        lp.seg[i].noise = profile_noise(profs[i], profs[i]->default_model, -1.0);
        lp.seg[i].frames = seglen;
        lp.seg[i].label = profs[i]->label;
    }
    lp.level_trace = calloc((size_t)frames, sizeof(int));
    lp.segment_trace = calloc((size_t)frames, sizeof(int));
    return lp;
}

TEST(adaptive_climbs_in_hospital_and_comes_back_down_in_toll) {
    const profile_t *profs[3] = {&PROFILE_TOLL_PLAZA, &PROFILE_HOSPITAL_IMAGING, &PROFILE_TOLL_PLAZA};
    linksim_params_t lp = adaptive(6000, 3, profs, 1500);
    linksim_result_t r;
    linksim_run(&lp, &r, NULL);
    /* compare the settled second half of each phase (frames are tagged with their segment) */
    long first_hosp = 0, first_toll2 = 0;
    while (first_hosp < lp.frames && lp.segment_trace[first_hosp] != 1) first_hosp++;
    while (first_toll2 < lp.frames && lp.segment_trace[first_toll2] != 2) first_toll2++;
    double toll1 = mean_level(lp.level_trace, lp.segment_trace, first_hosp / 2, first_hosp, 0);
    double hosp = mean_level(lp.level_trace, lp.segment_trace, (first_hosp + first_toll2) / 2, first_toll2, 1);
    double toll2 = mean_level(lp.level_trace, lp.segment_trace, (first_toll2 + lp.frames) / 2, lp.frames, 2);
    printf("    mean level: toll %.2f -> hospital %.2f -> toll %.2f (success %.4f)\n", toll1, hosp, toll2, r.success_rate);
    CHECK(hosp >= toll1 + 1.5);       /* raised the level for the bursty channel */
    CHECK(toll2 <= hosp - 1.5);       /* lowered it again afterwards */
    CHECK(r.success_rate > 0.97);     /* HARQ fallback + adaptation keep frames flowing */
    free(lp.level_trace);
    free(lp.segment_trace);
}

TEST(adaptive_does_not_oscillate_on_a_steady_channel) {
    const profile_t *toll[1] = {&PROFILE_TOLL_PLAZA}, *hosp[1] = {&PROFILE_HOSPITAL_IMAGING};
    const profile_t **chans[2] = {toll, hosp};
    for (int c = 0; c < 2; c++) {
        linksim_params_t lp = adaptive(4000, 1, chans[c], 0);
        linksim_result_t r;
        linksim_run(&lp, &r, NULL);
        long late = changes(lp.level_trace, 1000, lp.frames);   /* after converging */
        printf("    %s: %ld level changes in total, %ld in the last 3000 frames\n",
               chans[c][0]->label, r.level_changes, late);
        CHECK(late <= 4);
        free(lp.level_trace);
        free(lp.segment_trace);
    }
}

int main(void) {
    RUN(fec_has_no_retransmissions_and_constant_latency);
    RUN(arq_recovers_by_retransmitting);
    RUN(harq_beats_fec_and_needs_fewer_retx_than_arq);
    RUN(max_retries_bounds_transmissions);
    RUN(lost_feedback_costs_timeouts_but_not_correctness);
    RUN(reproducible);
    RUN(adaptive_climbs_in_hospital_and_comes_back_down_in_toll);
    RUN(adaptive_does_not_oscillate_on_a_steady_channel);
    return TEST_REPORT();
}
