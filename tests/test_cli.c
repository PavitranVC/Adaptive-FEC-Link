/*
 * Tests cli.c: flag parsing shared by vehicle, channel, tollgate and bench, and the derived
 * noise / controller parameters.
 * A failure means a flag is ignored or mis-ranged, a bad value is accepted instead of rejected,
 * or the same command line would configure the separate processes differently.
 */
#include <string.h>
#include "testlib.h"
#include "cli.h"

static int parse(cli_opts_t *o, int argc, const char **argv) {
    cli_defaults(o);
    return cli_parse(o, argc, (char **)argv);
}

/* Documented defaults (code, profile, ports, count, seed) are what a bare command line gets. */
TEST(defaults) {
    cli_opts_t o;
    cli_defaults(&o);
    CHECK(o.code == CODE_HAMMING74);
    CHECK(o.profile == &PROFILE_TOLL_PLAZA);
    CHECK_EQ_INT(o.model_set, 0);
    CHECK(o.p < 0);
    CHECK_EQ_INT(o.channel_port, 9000);
    CHECK_EQ_INT(o.tollgate_port, 9001);
    CHECK_EQ_INT(o.feedback_port, 9002);
    CHECK_EQ_INT(o.count, 20);
    CHECK_EQ_INT(o.seed, 1);
}

/* Each shared flag sets its field, including the --flag=value form; --model marks model_set. */
TEST(common_flags) {
    const char *argv[] = {"x", "--code", "secded84", "--profile", "hospital", "--model", "burst",
                          "--p", "0.25", "--seed", "77", "--count", "5", "--delay-ms", "10",
                          "--no-color", "--burst-len=9"};
    cli_opts_t o;
    CHECK_EQ_INT(parse(&o, 17, argv), 0);
    CHECK(o.code == CODE_SECDED84);
    CHECK(o.profile == &PROFILE_HOSPITAL_IMAGING);
    CHECK(o.model == NOISE_BURST && o.model_set);
    CHECK_NEAR(o.p, 0.25, 0);
    CHECK_EQ_INT(o.seed, 77);
    CHECK_EQ_INT(o.count, 5);
    CHECK_EQ_INT(o.delay_ms, 10);
    CHECK_EQ_INT(o.color, 0);
    CHECK_EQ_INT(o.burst_len, 9);
}

/* Without --model the noise model follows the profile; an explicit --model with --burst-len overrides the profile's burst length. */
TEST(model_defaults_to_profile_model) {
    const char *argv[] = {"x", "--profile", "hospital"};
    cli_opts_t o;
    CHECK_EQ_INT(parse(&o, 3, argv), 0);
    noise_params_t np = cli_noise_params(&o);
    CHECK(np.model == PROFILE_HOSPITAL_IMAGING.default_model);
    const char *argv2[] = {"x", "--profile", "toll", "--model", "burst", "--burst-len", "7"};
    CHECK_EQ_INT(parse(&o, 7, argv2), 0);
    np = cli_noise_params(&o);
    CHECK(np.model == NOISE_BURST);
    CHECK_EQ_INT(np.burst.length, 7);
}

/* Error paths: unknown code, probability above 1, unknown flag, missing value, trailing junk in an integer. */
TEST(rejects_bad_input) {
    fprintf(stderr, "    (6 error messages expected below)\n");
    cli_opts_t o;
    const char *a1[] = {"x", "--code", "turbo"};
    CHECK(parse(&o, 3, a1) != 0);
    const char *a2[] = {"x", "--p", "1.5"};
    CHECK(parse(&o, 3, a2) != 0);
    const char *a3[] = {"x", "--bogus"};
    CHECK(parse(&o, 2, a3) != 0);
    const char *a4[] = {"x", "--count"};
    CHECK(parse(&o, 2, a4) != 0);
    const char *a5[] = {"x", "--seed", "12abc"};
    CHECK(parse(&o, 3, a5) != 0);
}

/* Retransmission and controller flags reach cli_opts_t and adapt_params_t; an unknown strategy is rejected. */
TEST(strategy_and_adaptive_flags) {
    cli_opts_t o;
    cli_defaults(&o);
    CHECK(o.strategy == STRAT_FEC);                 /* default keeps v0.1 behaviour */
    const char *argv[] = {"x", "--strategy", "harq", "--rtt-ms", "30", "--timeout-ms", "150",
                          "--max-retries", "6", "--fb-drop", "0.1", "--window", "16",
                          "--up-threshold", "0.2", "--down-threshold", "0.02", "--start-level", "3",
                          "--schedule", "toll:10,hospital:10", "--jsonl", "live.jsonl"};
    CHECK_EQ_INT(parse(&o, 23, argv), 0);
    CHECK(o.jsonl_path && !strcmp(o.jsonl_path, "live.jsonl"));
    CHECK(o.strategy == STRAT_HARQ);
    CHECK_EQ_INT(o.rtt_ms, 30);
    CHECK_EQ_INT(o.timeout_ms, 150);
    CHECK_EQ_INT(o.max_retries, 6);
    CHECK_NEAR(o.fb_drop, 0.1, 0);
    adapt_params_t a = cli_adapt_params(&o);
    CHECK_EQ_INT(a.window, 16);
    CHECK_NEAR(a.up_threshold, 0.2, 0);
    CHECK_NEAR(a.down_threshold, 0.02, 0);
    CHECK_EQ_INT(a.start_level, 3);
    CHECK(o.schedule && !strcmp(o.schedule, "toll:10,hospital:10"));
    const char *bad[] = {"x", "--strategy", "tcp"};
    CHECK(parse(&o, 3, bad) != 0);
}

/* --corrupt-header defaults to 0 (header never corrupted) and is range-checked as a probability. */
TEST(failure_injection_flags) {
    cli_opts_t o;
    cli_defaults(&o);
    CHECK_NEAR(o.corrupt_header, 0.0, 0);           /* default: header never corrupted */
    const char *argv[] = {"x", "--corrupt-header", "0.05"};
    CHECK_EQ_INT(parse(&o, 3, argv), 0);
    CHECK_NEAR(o.corrupt_header, 0.05, 0);
    fprintf(stderr, "    (1 error message expected below)\n");
    const char *bad[] = {"x", "--corrupt-header", "2"};
    CHECK(parse(&o, 3, bad) != 0);
}

/* --seeds below 2 is rejected because a confidence interval needs at least two samples. */
TEST(seeds_flag) {
    cli_opts_t o;
    cli_defaults(&o);
    CHECK_EQ_INT(o.seeds, 0);                       /* default: single-seed behaviour */
    const char *argv[] = {"x", "--strategies", "--seeds", "20"};
    CHECK_EQ_INT(parse(&o, 4, argv), 0);
    CHECK_EQ_INT(o.seeds, 20);
    fprintf(stderr, "    (1 error message expected below)\n");
    const char *bad[] = {"x", "--seeds", "1"};       /* a CI needs at least 2 seeds */
    CHECK(parse(&o, 3, bad) != 0);
}

/* --ladder is parsed into the options only; the process-wide ladder selection stays untouched. */
TEST(ladder_flag) {
    cli_opts_t o;
    cli_defaults(&o);
    CHECK(o.ladder == LADDER_MIXED);
    const char *argv[] = {"x", "--ladder", "rs"};
    CHECK_EQ_INT(parse(&o, 3, argv), 0);
    CHECK(o.ladder == LADDER_RS);
    CHECK(ladder_selected() == LADDER_MIXED);       /* parsing alone selects nothing */
    fprintf(stderr, "    (1 error message expected below)\n");
    const char *bad[] = {"x", "--ladder", "turbo"};
    CHECK(parse(&o, 3, bad) != 0);
}

/* The ldpc code name and the --compare-codes list are accepted; the list is stored as given. */
TEST(ldpc_code_and_compare_codes_flag) {
    cli_opts_t o;
    cli_defaults(&o);
    CHECK(o.compare_codes == NULL);
    const char *argv[] = {"x", "--code", "ldpc", "--compare-codes", "bch3116,ldpc"};
    CHECK_EQ_INT(parse(&o, 5, argv), 0);
    CHECK(o.code == CODE_LDPC);
    CHECK(o.compare_codes && strcmp(o.compare_codes, "bch3116,ldpc") == 0);
}

/* --channel-port and --tollgate-port override the 9000/9001 defaults (--feedback-port is not covered). */
TEST(ports) {
    const char *argv[] = {"x", "--channel-port", "19000", "--tollgate-port", "19001"};
    cli_opts_t o;
    CHECK_EQ_INT(parse(&o, 5, argv), 0);
    CHECK_EQ_INT(o.channel_port, 19000);
    CHECK_EQ_INT(o.tollgate_port, 19001);
}

int main(void) {
    RUN(defaults);
    RUN(common_flags);
    RUN(model_defaults_to_profile_model);
    RUN(rejects_bad_input);
    RUN(strategy_and_adaptive_flags);
    RUN(failure_injection_flags);
    RUN(seeds_flag);
    RUN(ladder_flag);
    RUN(ldpc_code_and_compare_codes_flag);
    RUN(ports);
    return TEST_REPORT();
}
