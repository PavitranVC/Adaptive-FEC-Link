/*
 * noise.h - channel noise models acting on a vector of coded bits (one bit per uint8_t).
 *
 *  BSC   Binary Symmetric Channel: every bit flips independently with probability p.
 *
 *  GE    Gilbert-Elliott two-state Markov channel (models bursts):
 *              p_gb                       State G ("good"): flip prob e_good (small)
 *          G -------> B                   State B ("bad"):  flip prob e_bad  (large)
 *          ^ <------- |
 *              p_bg
 *        Per bit: flip with the probability of the current state, then maybe change state.
 *        Stationary probabilities: pi_B = p_gb / (p_gb + p_bg), pi_G = 1 - pi_B
 *        Average bit error rate:   BER  = pi_G * e_good + pi_B * e_bad
 *        Time spent in B per visit is geometric with mean 1 / p_bg bits (the burst length).
 *        The state is kept between frames (the interference does not reset per frame) and the
 *        initial state is drawn from the stationary distribution.
 *
 *  BURST One burst of exactly `length` bits per frame (with probability p_frame), placed
 *        uniformly at random. Classic definition of burst length: the first and the last bit
 *        of the burst are flipped, every bit in between flips with probability `density`.
 */
#ifndef NOISE_H
#define NOISE_H

#include <stddef.h>
#include <stdint.h>
#include "rng.h"

typedef enum { NOISE_BSC = 0, NOISE_GE = 1, NOISE_BURST = 2 } noise_model_t;

typedef struct { double p; } bsc_params_t;
typedef struct { double p_gb, p_bg, e_good, e_bad; } ge_params_t;
typedef struct { int length; double p_frame, density; } burst_params_t;

typedef struct {
    noise_model_t model;
    bsc_params_t bsc;
    ge_params_t ge;
    burst_params_t burst;
} noise_params_t;

typedef struct {
    noise_params_t params;
    rng_t rng;
    int ge_bad;         /* current Gilbert-Elliott state: 0 = good, 1 = bad */
} noise_t;

noise_params_t noise_bsc(double p);
noise_params_t noise_ge(double p_gb, double p_bg, double e_good, double e_bad);
noise_params_t noise_burst(int length, double p_frame, double density);

void noise_init(noise_t *ch, const noise_params_t *params, uint64_t seed);

/* Switch to new parameters mid-run (e.g. a scheduled profile change). The RNG stream continues;
 * the Gilbert-Elliott state is redrawn from the new stationary distribution. */
void noise_set_params(noise_t *ch, const noise_params_t *params);

/* Flips bits of `bits` in place according to the model. If mask != NULL, mask[i] = 1 exactly
 * where bit i was flipped (the ground-truth error pattern). Returns the number of flips. */
size_t noise_apply(noise_t *ch, uint8_t *bits, size_t n, uint8_t *mask);

/* Long-run expected fraction of flipped bits (BSC, GE). For BURST: expected flips per frame
 * divided by n is frame-size dependent, so this returns 0. */
double noise_expected_ber(const noise_params_t *params);

const char *noise_model_name(noise_model_t m);
int noise_model_from_name(const char *name, noise_model_t *out); /* 0 = ok, -1 = unknown */

/* Human-readable one-line description of the parameters, e.g. for logs. */
void noise_describe(const noise_params_t *params, char *out, size_t outlen);

#endif /* NOISE_H */
