/*
 * rng.h - small, fast, seeded pseudo-random number generator (xoshiro256**, Blackman & Vigna),
 * seeded through splitmix64. We implement it ourselves (instead of rand()) so that the same
 * --seed gives bit-identical experiments on Linux, WSL and macOS.
 */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct {
    uint64_t s[4];
} rng_t;

void rng_seed(rng_t *r, uint64_t seed);
uint64_t rng_next(rng_t *r);
double rng_uniform(rng_t *r);              /* uniform in [0, 1) with 53 random bits */
uint32_t rng_below(rng_t *r, uint32_t n);  /* uniform integer in [0, n), unbiased; n > 0 */
int rng_bernoulli(rng_t *r, double p);     /* 1 with probability p */

/* Derive an independent seed for a sub-stream, e.g. rng_derive(seed, "channel"). */
uint64_t rng_derive(uint64_t seed, const char *label);

#endif /* RNG_H */
