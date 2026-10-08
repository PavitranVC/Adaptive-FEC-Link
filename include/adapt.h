/*
 * adapt.h - the adaptive FEC controller (week-2 task 3) and the code ladder it climbs.
 *
 * CODE LADDER (weakest/cheapest -> strongest):
 *     L0 none  ->  L1 Hamming(7,4)  ->  L2 BCH(15,7)  ->  L3 BCH(31,16)  ->  L4 RS t=4  ->  L5 RS t=8
 *
 * INPUT: the receiver's sliding window over the last W frames at the current level (window.h):
 *     f    = win_fail / win_n                  measured frame failure rate
 *     BER^ = win_corrected / win_bits          estimated channel bit error rate
 *
 * RULES (evaluated once every W feedbacks, i.e. on disjoint windows, once the window is full):
 *   UP   if f > up_threshold                  the current code is visibly not coping.
 *   DOWN if f <= down_threshold                the current code copes, AND
 *        and P_fail(level-1, BER^) < down_threshold
 *                                              the next cheaper code is PREDICTED to cope too:
 *        P_fail = 1 - P(block ok)^blocks,  P(block ok) = sum_{i<=t} C(n,i) p^i (1-p)^(n-i)
 *        (binomial model of a random-error channel; for RS p is the byte error probability
 *         1 - (1-BER^)^8), AND
 *        the level has been held for at least hold * W frames.
 *
 * HYSTERESIS - why it does not flap:
 *   1. two different thresholds: up_threshold (0.20) > down_threshold (0.05); between them it holds.
 *   2. decisions are taken once per W frames on disjoint windows, and after any change the
 *      receiver's window restarts, so every decision needs W fresh frames at the new level.
 *      (Evaluating the sliding window on EVERY frame gave the up-rule ~W correlated chances per
 *      window to fire on noise and made the controller flap on a steady channel.)
 *   3. probe back-off: a step down is a "probe". If the controller has to step back up within 2W
 *      frames, the probe failed and the hold time doubles (1, 2, 4, ... max_hold windows) -
 *      on a bursty channel the binomial prediction is too optimistic, and this learns it.
 *      A probe that survives 2W frames resets the hold to 1.
 */
#ifndef ADAPT_H
#define ADAPT_H

#include "fec.h"
#include "feedback.h"

int ladder_levels(void);
fec_config_t ladder_config(int level);
const char *ladder_label(int level);              /* e.g. "L2 BCH(15,7)" */
int ladder_level_of(const fec_config_t *cfg);     /* -1 if not on the ladder */

/* Probability that a 128-bit payload frame fails on a binary symmetric channel with flip
 * probability ber (binomial model above). */
double adapt_predict_fail(const fec_config_t *cfg, double ber);

typedef struct {
    int window;            /* W, frames per decision (default 32) */
    double up_threshold;   /* default 0.20 */
    double down_threshold; /* default 0.05 */
    int start_level;       /* default 1 (Hamming(7,4)) */
    int min_level, max_level;
    int max_hold;          /* probe back-off cap in windows (default 16) */
} adapt_params_t;

typedef struct {
    adapt_params_t p;
    int level;
    long frames_at_level;  /* feedbacks received at the current level */
    long since_eval;       /* feedbacks since the last decision */
    int hold;              /* windows to wait before the next down-probe */
    int probing;           /* last move was down and is not yet confirmed */
    long changes, ups, downs, failed_probes;
    double last_fail, last_ber, last_pred;  /* last evaluated numbers, for logging */
} adapt_t;

void adapt_defaults(adapt_params_t *p);
void adapt_init(adapt_t *a, const adapt_params_t *p);

/* Feeds one feedback. Returns +1 (stepped up), -1 (stepped down) or 0. */
int adapt_update(adapt_t *a, const feedback_t *fb);

#endif /* ADAPT_H */
