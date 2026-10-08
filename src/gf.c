#include "gf.h"

int gf_init(gf_t *f, int m, unsigned poly) {
    if (m < 2 || m > 8 || !(poly & (1u << m))) return -1;
    f->m = m;
    f->n = (1 << m) - 1;
    f->poly = poly;
    for (int i = 0; i <= f->n; i++) f->log[i] = -1;
    /* Walk alpha^0, alpha^1, ...: multiply by x (shift left) and reduce by p(x) when the
     * x^m term appears (subtracting p(x) = XOR). */
    int a = 1;
    for (int i = 0; i < f->n; i++) {
        if (f->log[a] != -1) return -1;   /* alpha^i repeated early -> alpha not primitive */
        f->exp[i] = a;
        f->log[a] = i;
        a <<= 1;
        if (a & (1 << m)) a ^= (int)poly;
    }
    if (a != 1) return -1;                /* alpha^n must be 1 */
    for (int i = f->n; i < 2 * f->n; i++) f->exp[i] = f->exp[i - f->n];
    return 0;
}

int gf_exp(const gf_t *f, int i) {
    i %= f->n;
    if (i < 0) i += f->n;
    return f->exp[i];
}

int gf_log(const gf_t *f, int a) { return f->log[a]; }

int gf_mul(const gf_t *f, int a, int b) {
    if (a == 0 || b == 0) return 0;
    return f->exp[f->log[a] + f->log[b]];          /* sum < 2n, table is doubled */
}

int gf_div(const gf_t *f, int a, int b) {
    if (a == 0) return 0;
    return f->exp[f->log[a] - f->log[b] + f->n];   /* + n keeps the index non-negative */
}

int gf_inv(const gf_t *f, int a) { return f->exp[f->n - f->log[a]]; }

int gf_pow(const gf_t *f, int a, int e) {
    if (a == 0) return e == 0 ? 1 : 0;
    long le = (long)f->log[a] * e;
    return gf_exp(f, (int)(le % f->n));
}
