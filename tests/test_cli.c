/* Tests for the shared command-line parser. */
#include <string.h>
#include "testlib.h"
#include "cli.h"

static int parse(cli_opts_t *o, int argc, const char **argv) {
    cli_defaults(o);
    return cli_parse(o, argc, (char **)argv);
}

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

TEST(rejects_bad_input) {
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
    RUN(ports);
    return TEST_REPORT();
}
