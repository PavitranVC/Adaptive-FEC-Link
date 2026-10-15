#include <math.h>
#include "stats.h"

/* t_{0.975, df} for df = 1..30 from the standard table. */
static const double T975[30] = {
    12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
    2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
    2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};

double stats_t975(int df) {
    if (df < 1) df = 1;
    if (df <= 30) return T975[df - 1];
    /* df > 30: Cornish-Fisher expansion around the normal quantile z = 1.959964
     * t = z + (z^3 + z)/(4 df) + (5z^5 + 16z^3 + 3z)/(96 df^2), error < 1e-4 here */
    double z = 1.959964, d = (double)df;
    return z + (z * z * z + z) / (4 * d) +
           (5 * pow(z, 5) + 16 * z * z * z + 3 * z) / (96 * d * d);
}

stats_ci_t stats_mean_ci(const double *x, int n) {
    stats_ci_t s = {0, 0, 0, 0, 0, 0};
    s.n = n;
    if (n < 1) return s;
    for (int i = 0; i < n; i++) s.mean += x[i];
    s.mean /= n;
    if (n >= 2) {
        double ss = 0;
        for (int i = 0; i < n; i++) ss += (x[i] - s.mean) * (x[i] - s.mean);
        s.sd = sqrt(ss / (n - 1));
        s.half = stats_t975(n - 1) * s.sd / sqrt((double)n);
    }
    s.lo = s.mean - s.half;
    s.hi = s.mean + s.half;
    return s;
}
