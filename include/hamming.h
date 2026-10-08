/*
 * hamming.h - binary Hamming codes Hamming(n, k) with n = 2^r - 1, k = n - r.
 *   r = 3 -> Hamming(7,4),  r = 4 -> Hamming(15,11).
 *
 * Codeword layout (classic textbook form): code[i] holds position p = i + 1 (1-indexed).
 * Positions that are powers of two (1, 2, 4, 8) hold parity bits; the other positions hold the
 * data bits in order. Parity bit at position 2^j makes the XOR of all positions whose index has
 * bit j set equal to zero. Therefore the syndrome
 *       s = XOR of the indices p of all 1-bits in the received word
 * is 0 for a codeword and equals p exactly when a single bit at position p was flipped.
 * Minimum distance 3: corrects 1 error, but a 2-bit error yields s = p1 XOR p2 != 0 which points
 * at a third position, so the decoder silently "corrects" the wrong bit (miscorrection).
 */
#ifndef HAMMING_H
#define HAMMING_H

#include <stdint.h>

int hamming_n(int r); /* 2^r - 1 */
int hamming_k(int r); /* 2^r - 1 - r */

/* data: k bits -> code: n bits */
void hamming_encode(int r, const uint8_t *data, uint8_t *code);

/* Syndrome of an n-bit word: 0 = valid codeword, else 1-indexed position of a single error. */
int hamming_syndrome(int r, const uint8_t *code);

/* Corrects code in place (if syndrome != 0) and extracts the k data bits.
 * Returns the number of bits it changed: 0 or 1. (Hamming cannot detect failure.) */
int hamming_decode(int r, uint8_t *code, uint8_t *data);

#endif /* HAMMING_H */
