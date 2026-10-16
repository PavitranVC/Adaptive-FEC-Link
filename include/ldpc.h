/*
 * ldpc.h - a small (3,6)-regular LDPC code, n = 256, k = 128, with Gallager's bit-flipping
 * decoder (overnight run C, optional stretch).
 *
 * PARITY-CHECK MATRIX H (m x n = 128 x 256, binary)
 *   A word x (n bits) is a codeword  <=>  H x = 0 (mod 2).  Row j of H is one parity check:
 *   "the XOR of the 6 bits marked in row j is 0". Every column has 3 ones (each bit takes part in
 *   3 checks) and every row has 6 ones (each check looks at 6 bits): "(3,6)-regular".
 *   H is sparse (768 ones out of 32768), that is the "low-density" in LDPC.
 *   Tanner graph: bit nodes and check nodes, an edge for every 1 in H. A 4-cycle = two bits
 *   that share two checks; the bit-flipping decoder gets confused by those, so H is built with
 *   none (girth >= 6); it also avoids 6-cycles where it can, but for n = 256 the greedy
 *   construction cannot avoid all of them: the default code (LDPC_SEED) has girth 6.
 *
 * WHY IT IS LINEAR
 *   The code is the null space of H: if H a = 0 and H b = 0 then H (a XOR b) = Ha XOR Hb = 0.
 *   So the XOR of two codewords is a codeword, all-zero is a codeword, and the syndrome of a
 *   received word r = c XOR e is  s = H r = H c XOR H e = H e: it depends ONLY on the error
 *   pattern e, never on which codeword was sent. (The decoder below only looks at s, so its
 *   behaviour for an error pattern is the same for every codeword - the tests use that.)
 *
 * ENCODING (Gaussian elimination -> systematic generator)
 *   Row-reduce H over GF(2) to reduced row echelon form. Each row gets a pivot column; the 128
 *   pivot columns become PARITY positions, the other 128 columns are INFORMATION positions.
 *   Row j of the reduced matrix reads  x[par_pos[j]] + sum_i A[j][i] x[info_pos[i]] = 0,  so
 *       parity_j = sum_i A[j][i] * data_i   (mod 2).
 *   That is a systematic generator G = [I_k | A^T] (columns permuted): the data bits are copied
 *   unchanged to the information positions. Row operations do not change the null space, so the
 *   encoded word satisfies the ORIGINAL sparse H, which the decoder uses.
 *   The construction is retried (next RNG state) until H has full rank 128, so k = n - m = 128:
 *   the 128-bit payload (ID + CRC-32) is exactly one codeword, rate 1/2.
 *
 * DECODING (Gallager bit flipping, hard decision)
 *   1. s = H r. All zero -> r is a codeword, stop (success).
 *   2. For every bit count how many of its 3 checks are unsatisfied.
 *   3. Flip ALL bits that have the maximum count. Go to 1.
 *   After LDPC_MAX_ITER iterations with s != 0 the decoder gives up and reports a DETECTED
 *   failure. It only ever reports success when s = 0, so a "success" is always a codeword
 *   (possibly the wrong one, which the CRC-32 then has to catch).
 */
#ifndef LDPC_H
#define LDPC_H

#include <stdint.h>

#define LDPC_N 256          /* code length */
#define LDPC_M 128          /* parity checks (rows of H) */
#define LDPC_K 128          /* information bits = n - rank(H), rank is forced to m */
#define LDPC_WC 3           /* column weight: checks per bit */
#define LDPC_WR 6           /* row weight: bits per check */
#define LDPC_MAX_ITER 50    /* bit-flipping iterations before giving up (fixed, not tuned) */
#define LDPC_SEED 2026u     /* seed of the default code (fixed before any benchmark run) */

typedef struct {
    uint16_t col[LDPC_N][LDPC_WC];   /* H column view: the 3 checks of bit i */
    uint16_t row[LDPC_M][LDPC_WR];   /* H row view: the 6 bits of check j */
    uint16_t info_pos[LDPC_K];       /* codeword positions that carry the data bits */
    uint16_t par_pos[LDPC_M];        /* codeword positions that carry the parity bits */
    uint8_t A[LDPC_M][LDPC_K];       /* parity_j = sum_i A[j][i] data_i (mod 2) */
    int girth;                       /* shortest cycle in the Tanner graph (6 or 8 here) */
    int attempts;                    /* constructions tried until one met every requirement */
} ldpc_code_t;

/* Builds a (3,6)-regular H from the seeded RNG (no 4-cycles, full rank) and the systematic
 * encoder. Deterministic for a given seed. Returns 0 ok, -1 if no valid H was found. */
int ldpc_build(ldpc_code_t *c, uint64_t seed);

/* The code used by --code ldpc (built from LDPC_SEED on first use; NULL if that failed). */
const ldpc_code_t *ldpc_default(void);

/* 128 data bits -> 256 code bits (one bit per uint8_t). */
void ldpc_encode(const ldpc_code_t *c, const uint8_t *data, uint8_t *code);

/* s = H r. Writes s[0..m-1] if s != NULL; returns the number of unsatisfied checks. */
int ldpc_syndrome(const ldpc_code_t *c, const uint8_t *word, uint8_t *s);

/* Bit flipping with at most max_iter iterations. Repairs code in place, writes the k data bits.
 * Returns the number of bits it changed (>= 0) when the result is a codeword, -1 otherwise
 * (detected failure). *iters (if not NULL) gets the number of flipping iterations used. */
int ldpc_decode_iter(const ldpc_code_t *c, uint8_t *code, uint8_t *data, int max_iter, int *iters);

/* ldpc_decode_iter with LDPC_MAX_ITER. */
int ldpc_decode(const ldpc_code_t *c, uint8_t *code, uint8_t *data);

/* Length of the shortest cycle of the Tanner graph (0 if there is none). */
int ldpc_girth(const ldpc_code_t *c);

#endif /* LDPC_H */
