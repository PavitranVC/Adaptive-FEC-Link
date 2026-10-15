/*
 * stats.h - mean and 95% confidence interval over independent runs (seeds).
 *
 * With n runs x_1..x_n (one value per seed) the 95% confidence interval of the mean is
 *
 *     mean +- t_{0.975, n-1} * s / sqrt(n),      s = sample standard deviation (divide by n - 1)
 *
 * using Student's t distribution with n - 1 degrees of freedom (not the normal 1.96: with
 * 20 seeds t = 2.093, i.e. 7% wider). Assumes the per-seed values are independent and roughly
 * normal; seeds are independent RNG streams, and each value is itself an average over thousands
 * of frames (central limit theorem).
 */
#ifndef STATS_H
#define STATS_H

typedef struct {
    int n;
    double mean, sd, half;   /* half = half-width of the 95% CI (0 if n < 2) */
    double lo, hi;           /* mean - half, mean + half */
} stats_ci_t;

/* 0.975 quantile of Student's t with df degrees of freedom (df >= 1). */
double stats_t975(int df);

stats_ci_t stats_mean_ci(const double *x, int n);

#endif /* STATS_H */
