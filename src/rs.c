#include <string.h>
#include "bch.h"
#include "rs.h"

static int rs_init(rs_code_t *c, int k, int t) {
    memset(c, 0, sizeof *c);
    if (k < 1 || t < 1 || t > RS_MAX_T || k + 2 * t > 255) return -1;
    if (gf_init(&c->gf, 8, 0x11D) != 0) return -1;
    c->k = k;
    c->t = t;
    c->nroots = 2 * t;
    c->n = k + 2 * t;
    /* g(x) = prod_{j=1..2t} (x + alpha^j), built one factor at a time */
    c->g[0] = 1;
    for (int j = 1; j <= c->nroots; j++) {
        int root = gf_exp(&c->gf, j);
        for (int d = j; d >= 1; d--) c->g[d] = (uint8_t)(c->g[d - 1] ^ gf_mul(&c->gf, c->g[d], root));
        c->g[0] = (uint8_t)gf_mul(&c->gf, c->g[0], root);
    }
    return 0;
}

const rs_code_t *rs_get(int k, int t) {
    static rs_code_t cache[8];
    static int used = 0;
    for (int i = 0; i < used; i++)
        if (cache[i].k == k && cache[i].t == t) return &cache[i];
    rs_code_t tmp;
    if (rs_init(&tmp, k, t) != 0) return NULL;
    if (used == 8) used = 0;   /* tiny cache; the project only uses a handful of (k, t) */
    cache[used] = tmp;
    return &cache[used++];
}

void rs_encode(const rs_code_t *c, const uint8_t *data, uint8_t *cw) {
    uint8_t r[255];
    memset(r, 0, (size_t)c->n);
    memcpy(r + c->nroots, data, (size_t)c->k);            /* x^(2t) d(x) */
    for (int i = c->n - 1; i >= c->nroots; i--) {         /* long division by monic g(x) */
        int coef = r[i];
        if (!coef) continue;
        for (int j = 0; j <= c->nroots; j++)
            r[i - c->nroots + j] ^= (uint8_t)gf_mul(&c->gf, coef, c->g[j]);
    }
    memcpy(cw, r, (size_t)c->nroots);                      /* remainder = parity */
    memcpy(cw + c->nroots, data, (size_t)c->k);
}

int rs_syndromes(const rs_code_t *c, const uint8_t *r, uint8_t *S) {
    int zero = 1;
    for (int j = 1; j <= c->nroots; j++) {
        int x = gf_exp(&c->gf, j), acc = 0;
        for (int i = c->n - 1; i >= 0; i--) acc = gf_mul(&c->gf, acc, x) ^ r[i];  /* Horner */
        S[j - 1] = (uint8_t)acc;
        if (acc) zero = 0;
    }
    return zero;
}

/* p(x) at x, coefficients p[0..deg]. */
static int poly_eval(const gf_t *f, const uint8_t *p, int deg, int x) {
    int acc = 0;
    for (int d = deg; d >= 0; d--) acc = gf_mul(f, acc, x) ^ p[d];
    return acc;
}

int rs_decode(const rs_code_t *c, uint8_t *cw, uint8_t *data) {
    const gf_t *f = &c->gf;
    uint8_t S[2 * RS_MAX_T], lambda[2 * RS_MAX_T + 1], omega[2 * RS_MAX_T];
    int pos[2 * RS_MAX_T], val[2 * RS_MAX_T], nerr = 0;

    if (!rs_syndromes(c, cw, S)) {
        /* 1. error locator */
        int L = bch_berlekamp_massey(f, S, c->nroots, lambda);
        if (L > c->t) return -1;
        /* 2. Chien search over the positions that exist in the shortened code */
        for (int i = 0; i < c->n && nerr <= L; i++)
            if (poly_eval(f, lambda, L, gf_exp(f, -i)) == 0) pos[nerr++] = i;
        if (nerr != L) return -1;              /* roots outside 0..n-1 or missing: too many errors */
        /* 3. error evaluator Omega(x) = S(x) Lambda(x) mod x^(2t) */
        for (int d = 0; d < c->nroots; d++) {
            int acc = 0;
            for (int i = 0; i <= d && i <= L; i++) acc ^= gf_mul(f, lambda[i], S[d - i]);
            omega[d] = (uint8_t)acc;
        }
        /* 4. Forney: e = Omega(X^-1) / Lambda'(X^-1) */
        for (int e = 0; e < nerr; e++) {
            int xinv = gf_exp(f, -pos[e]), den = 0;
            for (int d = 1; d <= L; d += 2)    /* formal derivative: odd terms only */
                den ^= gf_mul(f, lambda[d], gf_pow(f, xinv, d - 1));
            if (den == 0) return -1;
            val[e] = gf_div(f, poly_eval(f, omega, c->nroots - 1, xinv), den);
            if (val[e] == 0) return -1;
        }
        for (int e = 0; e < nerr; e++) cw[pos[e]] ^= (uint8_t)val[e];
    }
    memcpy(data, cw + c->nroots, (size_t)c->k);
    return nerr;
}
