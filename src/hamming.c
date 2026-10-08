#include "hamming.h"

int hamming_n(int r) { return (1 << r) - 1; }
int hamming_k(int r) { return (1 << r) - 1 - r; }

/* A position p (1-indexed) is a parity position iff p is a power of two. */
static int is_parity_position(int p) { return (p & (p - 1)) == 0; }

/* XOR of the (1-indexed) positions of all set bits. */
int hamming_syndrome(int r, const uint8_t *code) {
    int n = hamming_n(r), s = 0;
    for (int p = 1; p <= n; p++)
        if (code[p - 1]) s ^= p;
    return s;
}

void hamming_encode(int r, const uint8_t *data, uint8_t *code) {
    int n = hamming_n(r), d = 0;
    /* 1) place data bits in the non-power-of-two positions, parity positions start at 0 */
    for (int p = 1; p <= n; p++)
        code[p - 1] = is_parity_position(p) ? 0 : data[d++];
    /* 2) the syndrome of this word tells which parity bits must be 1:
     *    setting parity bit 2^j toggles bit j of the syndrome, so set exactly the bits of s. */
    int s = hamming_syndrome(r, code);
    for (int j = 0; j < r; j++)
        code[(1 << j) - 1] = (uint8_t)((s >> j) & 1);
}

int hamming_decode(int r, uint8_t *code, uint8_t *data) {
    int n = hamming_n(r), d = 0, fixed = 0;
    int s = hamming_syndrome(r, code);
    if (s != 0) {            /* s is the position of the (assumed single) error */
        code[s - 1] ^= 1;
        fixed = 1;
    }
    for (int p = 1; p <= n; p++)
        if (!is_parity_position(p)) data[d++] = code[p - 1];
    return fixed;
}
