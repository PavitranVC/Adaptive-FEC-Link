/*
 * secded.h - extended Hamming(8,4): Single Error Correction, Double Error Detection.
 *
 * code[0..6] = Hamming(7,4) codeword, code[7] = overall parity of code[0..6] (even parity over
 * all 8 bits). Minimum distance 4. Decoding uses the Hamming syndrome s and the overall
 * parity check P (XOR of all 8 bits):
 *     s == 0, P == 0  -> no error
 *     s != 0, P == 1  -> odd number of errors, assume 1: flip position s
 *     s == 0, P == 1  -> the overall parity bit itself is wrong: flip code[7]
 *     s != 0, P == 0  -> even number (>= 2) of errors: DETECTED, do not correct
 */
#ifndef SECDED_H
#define SECDED_H

#include <stdint.h>

enum { SECDED_OK = 0, SECDED_CORRECTED = 1, SECDED_DOUBLE = -1 };

void secded84_encode(const uint8_t data[4], uint8_t code[8]);

/* Returns SECDED_OK, SECDED_CORRECTED (code repaired in place) or SECDED_DOUBLE.
 * data is always written (best effort, uncorrected bits on SECDED_DOUBLE). */
int secded84_decode(uint8_t code[8], uint8_t data[4]);

#endif /* SECDED_H */
