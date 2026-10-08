#include <string.h>
#include "bch.h"

/* ------------------------------------------------------------------------------------------
 * Generator polynomial
 * ------------------------------------------------------------------------------------------ */

/* Minimal polynomial of alpha^i: M_i(x) = prod over the cyclotomic coset {i, 2i, 4i, ...} (mod n)
 * of (x + alpha^j). Its coefficients turn out to be 0/1. Marks the coset members in `used`.
 * Returns the degree; coefficients in out[0..deg]. */
static int minimal_poly(const gf_t *f, int i, uint8_t *used, uint8_t *out) {
    int poly[BCH_MAX_N + 1] = {1};   /* GF(2^m) coefficients, starts as "1" */
    int deg = 0, j = i % f->n;
    do {
        /* poly(x) *= (x + alpha^j) */
        int root = gf_exp(f, j);
        for (int d = deg + 1; d >= 1; d--) poly[d] = poly[d - 1] ^ gf_mul(f, poly[d], root);
        poly[0] = gf_mul(f, poly[0], root);
        deg++;
        used[j] = 1;
        j = (2 * j) % f->n;          /* next conjugate: squaring alpha^j gives alpha^(2j) */
    } while (j != i % f->n);
    for (int d = 0; d <= deg; d++) out[d] = (uint8_t)poly[d]; /* 0 or 1 */
    return deg;
}

/* a(x) *= b(x) over GF(2). */
static int poly_mul_gf2(uint8_t *a, int deg_a, const uint8_t *b, int deg_b) {
    uint8_t r[2 * (BCH_MAX_N + 1)] = {0};
    for (int i = 0; i <= deg_a; i++)
        if (a[i])
            for (int j = 0; j <= deg_b; j++) r[i + j] ^= b[j];
    memcpy(a, r, (size_t)(deg_a + deg_b + 1));
    return deg_a + deg_b;
}

int bch_init(bch_code_t *c, int m, unsigned prim_poly, int t) {
    memset(c, 0, sizeof *c);
    if (gf_init(&c->gf, m, prim_poly) != 0 || c->gf.n > BCH_MAX_N || t < 1 || t > BCH_MAX_T)
        return -1;
    c->n = c->gf.n;
    c->t = t;
    uint8_t used[BCH_MAX_N + 1] = {0}, mp[BCH_MAX_N + 1];
    c->g[0] = 1;
    c->deg_g = 0;
    for (int i = 1; i <= 2 * t; i++) {       /* LCM: include each coset only once */
        if (used[i % c->n]) continue;
        int d = minimal_poly(&c->gf, i, used, mp);
        c->deg_g = poly_mul_gf2(c->g, c->deg_g, mp, d);
    }
    c->k = c->n - c->deg_g;
    return c->k > 0 ? 0 : -1;
}

const bch_code_t *bch157(void) {
    static bch_code_t c;
    static int ready = 0;
    if (!ready) { bch_init(&c, 4, 0x13, 2); ready = 1; }
    return &c;
}

const bch_code_t *bch3116(void) {
    static bch_code_t c;
    static int ready = 0;
    if (!ready) { bch_init(&c, 5, 0x25, 3); ready = 1; }
    return &c;
}

/* ------------------------------------------------------------------------------------------
 * Encoder: polynomial long division over GF(2)
 * ------------------------------------------------------------------------------------------ */

void bch_encode(const bch_code_t *c, const uint8_t *data, uint8_t *code) {
    int p = c->n - c->k;                      /* number of parity bits = deg g */
    uint8_t r[BCH_MAX_N];
    memset(r, 0, (size_t)c->n);
    for (int j = 0; j < c->k; j++) r[p + j] = data[j];    /* x^(n-k) d(x) */
    /* divide by g(x): cancel the highest remaining term each step */
    for (int i = c->n - 1; i >= p; i--)
        if (r[i])
            for (int j = 0; j <= c->deg_g; j++) r[i - c->deg_g + j] ^= c->g[j];
    for (int i = 0; i < p; i++) code[i] = r[i];           /* remainder = parity */
    for (int j = 0; j < c->k; j++) code[p + j] = data[j];
}

/* ------------------------------------------------------------------------------------------
 * Decoder
 * ------------------------------------------------------------------------------------------ */

int bch_syndromes(const bch_code_t *c, const uint8_t *r, uint8_t *S) {
    int all_zero = 1;
    for (int j = 1; j <= 2 * c->t; j++) {
        int s = 0;
        for (int i = 0; i < c->n; i++)
            if (r[i]) s ^= gf_exp(&c->gf, i * j);         /* r(alpha^j) = sum alpha^(i*j) */
        S[j - 1] = (uint8_t)s;
        if (s) all_zero = 0;
    }
    return all_zero;
}

/* Berlekamp-Massey. Invariant: after processing S_1..S_r, Lambda(x) is the shortest connection
 * polynomial (length L) such that S_k = sum_{i=1..L} Lambda_i S_{k-i} for all k <= r.
 *   d   = discrepancy: how wrong the current LFSR predicts the next syndrome
 *   B   = copy of Lambda from before the last length change, b = its discrepancy
 *   Update: Lambda(x) <- Lambda(x) - (d / b) x^shift B(x)  cancels the discrepancy. */
int bch_berlekamp_massey(const gf_t *f, const uint8_t *S, int nsyn, uint8_t *lambda) {
    int L = 0, shift = 1, b = 1;
    uint8_t B[2 * BCH_MAX_T + 2] = {1}, T[2 * BCH_MAX_T + 2];
    memset(lambda, 0, (size_t)nsyn + 1);
    lambda[0] = 1;
    for (int r = 0; r < nsyn; r++) {
        int d = S[r];
        for (int i = 1; i <= L; i++) d ^= gf_mul(f, lambda[i], S[r - i]);
        if (d == 0) { shift++; continue; }
        int coef = gf_div(f, d, b);
        memcpy(T, lambda, (size_t)nsyn + 1);
        for (int i = 0; i + shift <= nsyn; i++)
            lambda[i + shift] ^= (uint8_t)gf_mul(f, coef, B[i]);
        if (2 * L <= r) {                 /* LFSR must get longer */
            L = r + 1 - L;
            memcpy(B, T, (size_t)nsyn + 1);
            b = d;
            shift = 1;
        } else {
            shift++;
        }
    }
    return L;
}

int bch_chien(const bch_code_t *c, const uint8_t *lambda, int L, int *pos) {
    int found = 0;
    for (int i = 0; i < c->n; i++) {
        /* evaluate Lambda(alpha^-i) = sum_d lambda_d alpha^(-i d) */
        int v = 0;
        for (int d = 0; d <= L; d++)
            if (lambda[d]) v ^= gf_mul(&c->gf, lambda[d], gf_exp(&c->gf, -i * d));
        if (v == 0) pos[found++] = i;
    }
    return found;
}

int bch_decode(const bch_code_t *c, uint8_t *code, uint8_t *data) {
    uint8_t S[2 * BCH_MAX_T], lambda[2 * BCH_MAX_T + 1];
    int pos[BCH_MAX_N], fixed = 0, p = c->n - c->k;
    if (!bch_syndromes(c, code, S)) {
        int L = bch_berlekamp_massey(&c->gf, S, 2 * c->t, lambda);
        int roots = L <= c->t ? bch_chien(c, lambda, L, pos) : -1;
        if (roots != L) {                  /* more than t errors: give up, but say so */
            for (int j = 0; j < c->k; j++) data[j] = code[p + j];
            return -1;
        }
        for (int i = 0; i < roots; i++) code[pos[i]] ^= 1;
        fixed = roots;
    }
    for (int j = 0; j < c->k; j++) data[j] = code[p + j];
    return fixed;
}
