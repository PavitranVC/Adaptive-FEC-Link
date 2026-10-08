/*
 * gf.h - arithmetic in the finite field GF(2^m), 2 <= m <= 8.
 *
 * Elements are m-bit integers = polynomials over GF(2) of degree < m (bit i = coefficient of
 * x^i). Addition is XOR. Multiplication is polynomial multiplication modulo a PRIMITIVE
 * polynomial p(x) of degree m. "Primitive" means that alpha = x generates every non-zero element:
 *          GF(2^m)* = { alpha^0, alpha^1, ..., alpha^(2^m - 2) },   alpha^(2^m - 1) = 1.
 * So we tabulate exp[i] = alpha^i and log[alpha^i] = i once, and then
 *          a * b = alpha^(log a + log b),   a / b = alpha^(log a - log b)   (exponents mod 2^m - 1).
 *
 * Fields used in this project:
 *   GF(16)  p(x) = x^4 + x + 1            (0x13)   for BCH(15,7)
 *   GF(32)  p(x) = x^5 + x^2 + 1          (0x25)   for BCH(31,16)
 *   GF(256) p(x) = x^8 + x^4 + x^3 + x^2 + 1 (0x11D) for Reed-Solomon
 */
#ifndef GF_H
#define GF_H

typedef struct {
    int m;          /* field is GF(2^m) */
    int n;          /* 2^m - 1 = order of alpha */
    unsigned poly;  /* primitive polynomial including the x^m term */
    int exp[512];   /* exp[i] = alpha^i for 0 <= i < 2n (doubled to skip a modulo) */
    int log[256];   /* log[alpha^i] = i, log[0] undefined (-1) */
} gf_t;

/* Returns 0 on success, -1 if m is out of range or poly is not primitive. */
int gf_init(gf_t *f, int m, unsigned poly);

int gf_exp(const gf_t *f, int i);            /* alpha^i, any integer i */
int gf_log(const gf_t *f, int a);            /* a != 0 */
int gf_mul(const gf_t *f, int a, int b);
int gf_div(const gf_t *f, int a, int b);     /* b != 0 */
int gf_inv(const gf_t *f, int a);            /* a != 0 */
int gf_pow(const gf_t *f, int a, int e);     /* a^e, e may be negative if a != 0 */

#endif /* GF_H */
