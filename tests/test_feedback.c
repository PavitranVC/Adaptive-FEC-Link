/* Tests for the ACK/NAK feedback packet and the stop-and-wait sender state machine. */
#include <string.h>
#include "testlib.h"
#include "feedback.h"
#include "sender.h"

TEST(feedback_roundtrip) {
    feedback_t f, g;
    memset(&f, 0, sizeof f);
    f.type = FB_NAK;
    f.seq = 4242;
    f.attempt = 3;
    f.level = 2;
    f.win_n = 32;
    f.win_fail = 5;
    f.win_corrected = 1234;
    f.win_bits = 9120;
    uint8_t wire[FEEDBACK_BYTES + 4];
    CHECK_EQ_INT(feedback_serialize(&f, wire, sizeof wire), FEEDBACK_BYTES);
    CHECK(feedback_parse(wire, FEEDBACK_BYTES, &g) == 0);
    CHECK(memcmp(&f, &g, sizeof f) == 0);
    CHECK(feedback_parse(wire, FEEDBACK_BYTES - 1, &g) != 0);   /* truncated */
    wire[0] = 'X';
    CHECK(feedback_parse(wire, FEEDBACK_BYTES, &g) != 0);       /* bad magic */
}

TEST(strategy_names) {
    strategy_t s;
    CHECK(strategy_from_name("fec", &s) == 0 && s == STRAT_FEC);
    CHECK(strategy_from_name("arq", &s) == 0 && s == STRAT_ARQ);
    CHECK(strategy_from_name("harq", &s) == 0 && s == STRAT_HARQ);
    CHECK(strategy_from_name("adaptive", &s) == 0 && s == STRAT_ADAPTIVE);
    CHECK(strategy_from_name("tcp", &s) != 0);
    CHECK(strcmp(strategy_name(STRAT_HARQ), "harq") == 0);
}

static sender_t make(strategy_t s, int max_retries) {
    sender_t snd;
    fec_config_t fc = fec_config(CODE_BCH157);
    sender_init(&snd, s, &fc, NULL, max_retries);
    return snd;
}

static feedback_t fb(int type, uint32_t seq, int attempt) {
    feedback_t f;
    memset(&f, 0, sizeof f);
    f.type = (uint8_t)type;
    f.seq = seq;
    f.attempt = (uint8_t)attempt;
    return f;
}

TEST(fec_never_waits_and_keeps_its_code) {
    sender_t s = make(STRAT_FEC, 3);
    sender_begin_frame(&s, 0);
    CHECK_EQ_INT(sender_needs_feedback(&s), 0);
    CHECK(sender_config(&s).code == CODE_BCH157);
}

TEST(arq_sends_uncoded_with_crc_only) {
    sender_t s = make(STRAT_ARQ, 3);
    sender_begin_frame(&s, 0);
    CHECK_EQ_INT(sender_needs_feedback(&s), 1);
    CHECK(sender_config(&s).code == CODE_NONE);
}

TEST(ack_finishes_nak_retries_until_limit) {
    sender_t s = make(STRAT_HARQ, 2);
    sender_begin_frame(&s, 7);
    CHECK(sender_config(&s).code == CODE_BCH157);          /* HARQ keeps the FEC code */
    feedback_t nak0 = fb(FB_NAK, 7, 0), nak1 = fb(FB_NAK, 7, 1), nak2 = fb(FB_NAK, 7, 2);
    CHECK_EQ_INT(sender_on_feedback(&s, &nak0, NULL), SEND_RETRY);
    CHECK_EQ_INT(s.attempt, 1);
    CHECK_EQ_INT(sender_on_feedback(&s, &nak1, NULL), SEND_RETRY);
    CHECK_EQ_INT(sender_on_feedback(&s, &nak2, NULL), SEND_GIVE_UP);  /* 1 + 2 retries used */
    CHECK_EQ_INT(s.retransmissions, 2);
    sender_begin_frame(&s, 8);
    feedback_t ack = fb(FB_ACK, 8, 0);
    CHECK_EQ_INT(sender_on_feedback(&s, &ack, NULL), SEND_DONE);
}

TEST(stale_and_duplicate_feedback_is_ignored) {
    sender_t s = make(STRAT_ARQ, 5);
    sender_begin_frame(&s, 10);
    feedback_t old_seq = fb(FB_ACK, 9, 0);                  /* duplicate ACK of previous frame */
    CHECK_EQ_INT(sender_on_feedback(&s, &old_seq, NULL), SEND_IGNORE);
    CHECK_EQ_INT(sender_on_timeout(&s), SEND_RETRY);        /* feedback lost -> timeout */
    CHECK_EQ_INT(s.timeouts, 1);
    feedback_t late_nak = fb(FB_NAK, 10, 0);                /* NAK of attempt 0 arrives late */
    CHECK_EQ_INT(sender_on_feedback(&s, &late_nak, NULL), SEND_IGNORE);
    CHECK_EQ_INT(s.attempt, 1);
    feedback_t late_ack = fb(FB_ACK, 10, 0);                /* a late ACK still means delivered */
    CHECK_EQ_INT(sender_on_feedback(&s, &late_ack, NULL), SEND_DONE);
}

TEST(timeout_gives_up_after_max_retries) {
    sender_t s = make(STRAT_ARQ, 1);
    sender_begin_frame(&s, 0);
    CHECK_EQ_INT(sender_on_timeout(&s), SEND_RETRY);
    CHECK_EQ_INT(sender_on_timeout(&s), SEND_GIVE_UP);
}

int main(void) {
    RUN(feedback_roundtrip);
    RUN(strategy_names);
    RUN(fec_never_waits_and_keeps_its_code);
    RUN(arq_sends_uncoded_with_crc_only);
    RUN(ack_finishes_nak_retries_until_limit);
    RUN(stale_and_duplicate_feedback_is_ignored);
    RUN(timeout_gives_up_after_max_retries);
    return TEST_REPORT();
}
