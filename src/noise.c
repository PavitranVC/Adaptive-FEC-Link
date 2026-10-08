#include <stdio.h>
#include <string.h>
#include "noise.h"

noise_params_t noise_bsc(double p) {
    noise_params_t np;
    memset(&np, 0, sizeof np);
    np.model = NOISE_BSC;
    np.bsc.p = p;
    return np;
}

noise_params_t noise_ge(double p_gb, double p_bg, double e_good, double e_bad) {
    noise_params_t np;
    memset(&np, 0, sizeof np);
    np.model = NOISE_GE;
    np.ge.p_gb = p_gb;
    np.ge.p_bg = p_bg;
    np.ge.e_good = e_good;
    np.ge.e_bad = e_bad;
    return np;
}

noise_params_t noise_burst(int length, double p_frame, double density) {
    noise_params_t np;
    memset(&np, 0, sizeof np);
    np.model = NOISE_BURST;
    np.burst.length = length;
    np.burst.p_frame = p_frame;
    np.burst.density = density;
    return np;
}

static double ge_pi_bad(const ge_params_t *g) {
    double denom = g->p_gb + g->p_bg;
    return denom > 0 ? g->p_gb / denom : 0.0;
}

void noise_init(noise_t *ch, const noise_params_t *params, uint64_t seed) {
    ch->params = *params;
    rng_seed(&ch->rng, seed);
    /* start the Markov chain in its stationary distribution */
    ch->ge_bad = params->model == NOISE_GE ? rng_bernoulli(&ch->rng, ge_pi_bad(&params->ge)) : 0;
}

void noise_set_params(noise_t *ch, const noise_params_t *params) {
    ch->params = *params;
    ch->ge_bad = params->model == NOISE_GE ? rng_bernoulli(&ch->rng, ge_pi_bad(&params->ge)) : 0;
}

static void flip(uint8_t *bits, uint8_t *mask, size_t i) {
    bits[i] ^= 1;
    if (mask) mask[i] = 1;
}

static size_t apply_bsc(noise_t *ch, uint8_t *bits, size_t n, uint8_t *mask) {
    size_t flips = 0;
    for (size_t i = 0; i < n; i++)
        if (rng_bernoulli(&ch->rng, ch->params.bsc.p)) { flip(bits, mask, i); flips++; }
    return flips;
}

static size_t apply_ge(noise_t *ch, uint8_t *bits, size_t n, uint8_t *mask) {
    const ge_params_t *g = &ch->params.ge;
    size_t flips = 0;
    for (size_t i = 0; i < n; i++) {
        double e = ch->ge_bad ? g->e_bad : g->e_good;
        if (rng_bernoulli(&ch->rng, e)) { flip(bits, mask, i); flips++; }
        /* Markov transition for the next bit */
        if (ch->ge_bad) { if (rng_bernoulli(&ch->rng, g->p_bg)) ch->ge_bad = 0; }
        else            { if (rng_bernoulli(&ch->rng, g->p_gb)) ch->ge_bad = 1; }
    }
    return flips;
}

static size_t apply_burst(noise_t *ch, uint8_t *bits, size_t n, uint8_t *mask) {
    const burst_params_t *b = &ch->params.burst;
    if (n == 0 || b->length <= 0 || !rng_bernoulli(&ch->rng, b->p_frame)) return 0;
    size_t len = (size_t)b->length > n ? n : (size_t)b->length;
    size_t start = rng_below(&ch->rng, (uint32_t)(n - len + 1));
    size_t flips = 0;
    for (size_t j = 0; j < len; j++) {
        int edge = (j == 0 || j == len - 1);
        if (edge || rng_bernoulli(&ch->rng, b->density)) { flip(bits, mask, start + j); flips++; }
    }
    return flips;
}

size_t noise_apply(noise_t *ch, uint8_t *bits, size_t n, uint8_t *mask) {
    if (mask) memset(mask, 0, n);
    switch (ch->params.model) {
    case NOISE_BSC:   return apply_bsc(ch, bits, n, mask);
    case NOISE_GE:    return apply_ge(ch, bits, n, mask);
    case NOISE_BURST: return apply_burst(ch, bits, n, mask);
    }
    return 0;
}

double noise_expected_ber(const noise_params_t *np) {
    switch (np->model) {
    case NOISE_BSC: return np->bsc.p;
    case NOISE_GE: {
        double pb = ge_pi_bad(&np->ge);
        return (1.0 - pb) * np->ge.e_good + pb * np->ge.e_bad;
    }
    case NOISE_BURST: return 0.0;
    }
    return 0.0;
}

static const char *const MODEL_NAMES[] = {"bsc", "ge", "burst"};

const char *noise_model_name(noise_model_t m) {
    return (m >= NOISE_BSC && m <= NOISE_BURST) ? MODEL_NAMES[m] : "?";
}

int noise_model_from_name(const char *name, noise_model_t *out) {
    for (int i = 0; i < 3; i++)
        if (strcmp(name, MODEL_NAMES[i]) == 0) { *out = (noise_model_t)i; return 0; }
    return -1;
}

void noise_describe(const noise_params_t *np, char *out, size_t outlen) {
    switch (np->model) {
    case NOISE_BSC:
        snprintf(out, outlen, "bsc p=%g", np->bsc.p);
        break;
    case NOISE_GE:
        snprintf(out, outlen, "ge p_gb=%g p_bg=%g e_good=%g e_bad=%g (mean burst %.1f bits, BER %.4g)",
                 np->ge.p_gb, np->ge.p_bg, np->ge.e_good, np->ge.e_bad,
                 np->ge.p_bg > 0 ? 1.0 / np->ge.p_bg : 0.0, noise_expected_ber(np));
        break;
    case NOISE_BURST:
        snprintf(out, outlen, "burst L=%d p_frame=%g density=%g",
                 np->burst.length, np->burst.p_frame, np->burst.density);
        break;
    }
}
