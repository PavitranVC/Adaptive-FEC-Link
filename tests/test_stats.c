/* Tests for the confidence-interval helper used by `bench --strategies --seeds N`. */
#include "testlib.h"
#include "stats.h"

TEST(t_quantiles_match_the_table) {
    /* two-sided 95% => 0.975 quantile of Student's t (standard statistics tables) */
    CHECK_NEAR(stats_t975(1), 12.706, 0.001);
    CHECK_NEAR(stats_t975(4), 2.776, 0.001);
    CHECK_NEAR(stats_t975(19), 2.093, 0.001);
    CHECK_NEAR(stats_t975(30), 2.042, 0.001);
    CHECK_NEAR(stats_t975(40), 2.021, 0.002);   /* beyond the table: series expansion */
    CHECK_NEAR(stats_t975(120), 1.980, 0.002);
    CHECK_NEAR(stats_t975(100000), 1.960, 0.001);
    double prev = 1e9;                          /* decreasing in df */
    int mono = 1;
    for (int df = 1; df < 500; df++) {
        double t = stats_t975(df);
        if (!(t < prev)) mono = 0;
        prev = t;
    }
    CHECK(mono);
}

TEST(mean_and_ci_of_known_sample) {
    const double x[] = {1, 2, 3, 4, 5};
    stats_ci_t s = stats_mean_ci(x, 5);
    CHECK_EQ_INT(s.n, 5);
    CHECK_NEAR(s.mean, 3.0, 1e-12);
    CHECK_NEAR(s.sd, 1.5811388, 1e-6);          /* sample sd (n - 1) */
    CHECK_NEAR(s.half, 2.776445 * 1.5811388 / 2.2360680, 1e-3);
    CHECK_NEAR(s.lo, s.mean - s.half, 1e-12);
    CHECK_NEAR(s.hi, s.mean + s.half, 1e-12);
}

TEST(degenerate_samples) {
    const double same[] = {0.5, 0.5, 0.5};
    stats_ci_t s = stats_mean_ci(same, 3);
    CHECK_NEAR(s.mean, 0.5, 1e-15);
    CHECK_NEAR(s.half, 0.0, 1e-15);
    const double one[] = {7.0};
    s = stats_mean_ci(one, 1);                  /* no spread estimate from one value */
    CHECK_NEAR(s.mean, 7.0, 0);
    CHECK_NEAR(s.half, 0.0, 0);
    s = stats_mean_ci(one, 0);
    CHECK_EQ_INT(s.n, 0);
}

int main(void) {
    printf("test_stats\n");
    RUN(t_quantiles_match_the_table);
    RUN(mean_and_ci_of_known_sample);
    RUN(degenerate_samples);
    return TEST_REPORT();
}
