/*
 * Tests schedule.c: parsing of "profile:frames,..." specs and mapping transmission index to segment.
 * A failure means the channel would switch interference profile at the wrong transmission, or
 * accept a malformed --schedule instead of refusing to start.
 */
#include "testlib.h"
#include "schedule.h"

/* Well-formed specs yield the right profiles, frame counts and total. */
TEST(parse_valid) {
    schedule_t s;
    CHECK_EQ_INT(schedule_parse("toll:200,hospital:150,toll:50", &s), 0);
    CHECK_EQ_INT(s.n, 3);
    CHECK(s.seg[0].profile == &PROFILE_TOLL_PLAZA && s.seg[0].frames == 200);
    CHECK(s.seg[1].profile == &PROFILE_HOSPITAL_IMAGING && s.seg[1].frames == 150);
    CHECK_EQ_INT(schedule_total(&s), 400);
    CHECK_EQ_INT(schedule_parse("hospital:10", &s), 0);
    CHECK_EQ_INT(s.n, 1);
}

/* Empty, missing count, zero count, unknown profile, empty item and trailing garbage are all rejected. */
TEST(parse_invalid) {
    schedule_t s;
    CHECK(schedule_parse("", &s) != 0);
    CHECK(schedule_parse("toll", &s) != 0);
    CHECK(schedule_parse("toll:", &s) != 0);
    CHECK(schedule_parse("toll:0", &s) != 0);
    CHECK(schedule_parse("mars:10", &s) != 0);
    CHECK(schedule_parse("toll:10,,hospital:5", &s) != 0);
    CHECK(schedule_parse("toll:10x", &s) != 0);
}

/* Counts strtol would accept (leading space, sign), overflowing counts and sums above INT_MAX are rejected. */
TEST(parse_rejects_sign_space_and_overflow) {
    schedule_t s;
    CHECK(schedule_parse("toll: 5", &s) != 0);
    CHECK(schedule_parse("toll:+5", &s) != 0);
    CHECK(schedule_parse("toll:-5", &s) != 0);
    CHECK(schedule_parse("toll:99999999999999999999999", &s) != 0);   /* strtol saturates */
    CHECK(schedule_parse("toll:2147483648", &s) != 0);
    CHECK(schedule_parse("toll:2147483647,hospital:1", &s) != 0);     /* total would exceed INT_MAX */
    CHECK_EQ_INT(schedule_parse("toll:2147483647", &s), 0);
    CHECK_EQ_INT(schedule_total(&s), 2147483647L);
}

/* Segment switch happens exactly at the cumulative boundary, and the last segment stays in force past the end. */
TEST(segment_lookup_and_last_segment_persists) {
    schedule_t s;
    schedule_parse("toll:3,hospital:2", &s);
    const int want[8] = {0, 0, 0, 1, 1, 1, 1, 1};
    for (long tx = 0; tx < 8; tx++) CHECK_EQ_INT(schedule_segment_at(&s, tx), want[tx]);
}

/* schedule_to_linksim copies segment count, frames and each profile's default noise model. */
TEST(to_linksim_segments) {
    schedule_t s;
    schedule_parse("toll:100,hospital:100", &s);
    linksim_params_t lp;
    linksim_defaults(&lp);
    schedule_to_linksim(&s, 0, NOISE_BSC, -1.0, &lp);
    CHECK_EQ_INT(lp.nseg, 2);
    CHECK(lp.seg[0].noise.model == PROFILE_TOLL_PLAZA.default_model);
    CHECK(lp.seg[1].noise.model == PROFILE_HOSPITAL_IMAGING.default_model);
    CHECK_EQ_INT(lp.seg[0].frames, 100);
}

int main(void) {
    RUN(parse_valid);
    RUN(parse_invalid);
    RUN(parse_rejects_sign_space_and_overflow);
    RUN(segment_lookup_and_last_segment_persists);
    RUN(to_linksim_segments);
    return TEST_REPORT();
}
