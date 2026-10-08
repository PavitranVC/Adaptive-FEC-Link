/*
 * rs.h - shortened Reed-Solomon codes over GF(256), symbols = bytes.
 *
 * RS(n, k) with n = k + 2t <= 255 corrects any t wrong BYTES, whatever the bit errors inside a
 * byte are. A burst of L bits touches at most ceil((L + 7) / 8) bytes, so RS is naturally good
 * against bursts: t = 4 corrects every burst of up to 8 * (t - 1) + 1 = 25 bits.
 *
 * "Shortened": the full code has length 255; we use only k = 16 data bytes (our 128-bit payload)
 * and imagine the remaining 255 - n high-order positions as fixed zeros that are never sent.
 *
 * Codeword polynomial c(x) = sum cw[i] x^i, systematic:
 *     cw[0 .. 2t-1] = parity = x^(2t) d(x) mod g(x),   cw[2t .. n-1] = data bytes
 * Generator: g(x) = (x + alpha^1)(x + alpha^2)...(x + alpha^(2t))  (alpha = 2, p(x) = 0x11D)
 *
 * Decoder: syndromes S_j = r(alpha^j) -> Berlekamp-Massey (shared with BCH) gives the error
 * locator Lambda(x) -> Chien search finds positions (only 0..n-1 are allowed: shortened) ->
 * Forney's formula gives each error VALUE (a byte, unlike binary BCH where it is always 1):
 *     Omega(x) = S(x) Lambda(x) mod x^(2t),  S(x) = S_1 + S_2 x + ... + S_2t x^(2t-1)
 *     e_i = Omega(X^-1) / Lambda'(X^-1),     X = alpha^i
 * (Lambda' is the formal derivative; in characteristic 2 only odd powers survive.)
 */
#ifndef RS_H
#define RS_H

#include <stdint.h>
#include "gf.h"

#define RS_MAX_T 16

typedef struct {
    gf_t gf;
    int k, n, t, nroots;          /* nroots = 2t parity bytes */
    uint8_t g[2 * RS_MAX_T + 1];  /* generator coefficients g[0..nroots], g[nroots] = 1 */
} rs_code_t;

/* Cached code with k data bytes and t correctable bytes; NULL if the parameters are invalid. */
const rs_code_t *rs_get(int k, int t);

void rs_encode(const rs_code_t *c, const uint8_t *data, uint8_t *cw);

/* S[0..2t-1] = S_1..S_2t; returns 1 if all are zero (valid codeword). */
int rs_syndromes(const rs_code_t *c, const uint8_t *r, uint8_t *S);

/* Repairs cw in place, writes k data bytes. Returns the number of corrected BYTES or -1 when an
 * uncorrectable pattern is detected (cw is then left unchanged). */
int rs_decode(const rs_code_t *c, uint8_t *cw, uint8_t *data);

#endif /* RS_H */
