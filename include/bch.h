/*
 * bch.h - binary, narrow-sense, primitive BCH codes of length n = 2^m - 1.
 *
 * Construction: the generator polynomial g(x) is the least common multiple of the minimal
 * polynomials of alpha^1, alpha^2, ..., alpha^(2t). Every codeword c(x) is a multiple of g(x),
 * hence c(alpha^j) = 0 for j = 1..2t; the BCH bound then guarantees minimum distance >= 2t + 1,
 * i.e. any t bit errors can be corrected. k = n - deg g(x).
 *     BCH(15,7):  m = 4, t = 2, g(x) = x^8 + x^7 + x^6 + x^4 + 1
 *     BCH(31,16): m = 5, t = 3, deg g(x) = 15
 *
 * Bit i of a codeword array = coefficient of x^i. Systematic encoding:
 *     c(x) = x^(n-k) d(x) + [ x^(n-k) d(x) mod g(x) ]
 * so code[0 .. n-k-1] are parity bits and code[n-k .. n-1] are the k data bits.
 *
 * Decoding (received r(x) = c(x) + e(x)):
 *   1. Syndromes  S_j = r(alpha^j) = e(alpha^j), j = 1..2t.  All zero -> no error.
 *   2. Berlekamp-Massey finds the shortest LFSR generating S_1..S_2t; its connection
 *      polynomial is the error-locator  Lambda(x) = prod_l (1 + X_l x),  X_l = alpha^(pos_l).
 *   3. Chien search: try every position i; Lambda(alpha^-i) == 0  <=>  error at position i.
 *   4. Binary code: the error value is always 1, so just flip those bits.
 *   If the number of roots found differs from deg Lambda (or deg Lambda > t) the error pattern
 *   is beyond the code's power: the decoder reports a DETECTED failure.
 */
#ifndef BCH_H
#define BCH_H

#include <stdint.h>
#include "gf.h"

#define BCH_MAX_N 63
#define BCH_MAX_T 8

typedef struct {
    gf_t gf;
    int n, k, t;
    int deg_g;
    uint8_t g[BCH_MAX_N + 1];  /* generator coefficients g[0..deg_g], binary */
} bch_code_t;

/* Builds the BCH code of length 2^m - 1 correcting t errors. Returns 0 ok, -1 error. */
int bch_init(bch_code_t *c, int m, unsigned prim_poly, int t);

/* Ready-made codes (built on first use). */
const bch_code_t *bch157(void);
const bch_code_t *bch3116(void);

void bch_encode(const bch_code_t *c, const uint8_t *data, uint8_t *code);

/* S[0..2t-1] = S_1..S_2t. Returns 1 if all syndromes are zero (valid codeword). */
int bch_syndromes(const bch_code_t *c, const uint8_t *r, uint8_t *S);

/* Berlekamp-Massey over GF(2^m). lambda gets nsyn + 1 coefficients; returns L = deg Lambda. */
int bch_berlekamp_massey(const gf_t *f, const uint8_t *S, int nsyn, uint8_t *lambda);

/* Chien search: writes error positions into pos, returns how many roots were found. */
int bch_chien(const bch_code_t *c, const uint8_t *lambda, int L, int *pos);

/* Repairs code in place, writes the k data bits.
 * Returns the number of corrected bits, or -1 for a detected decoding failure. */
int bch_decode(const bch_code_t *c, uint8_t *code, uint8_t *data);

#endif /* BCH_H */
