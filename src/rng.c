#include "rng.h"

/* splitmix64: turns any 64-bit seed (even 0) into well-mixed state words. */
static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

void rng_seed(rng_t *r, uint64_t seed) {
    for (int i = 0; i < 4; i++) r->s[i] = splitmix64(&seed);
}

uint64_t rng_next(rng_t *r) {
    uint64_t *s = r->s;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

double rng_uniform(rng_t *r) {
    return (double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0); /* / 2^53 */
}

uint32_t rng_below(rng_t *r, uint32_t n) {
    /* rejection sampling: discard the top partial range so every value is equally likely */
    uint64_t limit = UINT64_MAX - (UINT64_MAX % n);
    uint64_t x;
    do { x = rng_next(r); } while (x >= limit);
    return (uint32_t)(x % n);
}

int rng_bernoulli(rng_t *r, double p) {
    if (p <= 0.0) return 0;
    if (p >= 1.0) return 1;
    return rng_uniform(r) < p;
}

uint64_t rng_derive(uint64_t seed, const char *label) {
    /* FNV-1a hash of the label mixed into the seed */
    uint64_t h = 0xCBF29CE484222325ull;
    for (const char *c = label; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001B3ull;
    uint64_t x = seed ^ h;
    return splitmix64(&x);
}
