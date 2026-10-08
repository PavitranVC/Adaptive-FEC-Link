/*
 * interleave.h - block interleaver for the coded bit vector.
 *
 * The n coded bits are written ROW by row into a matrix with `depth` rows and
 * C = ceil(n / depth) columns, and transmitted COLUMN by column (empty cells of the last row are
 * skipped). Two bits that are adjacent on the air were C positions apart in the codeword, so a
 * channel burst of up to `depth` bits is spread over `depth` different rows: with C >= block
 * length n_b each code block sees at most one error of that burst (guaranteed when depth divides n).
 *
 *     depth 2, n = 6:   rows [0 1 2]      sent: 0 3 1 4 2 5
 *                            [3 4 5]
 */
#ifndef INTERLEAVE_H
#define INTERLEAVE_H

#include <stddef.h>
#include <stdint.h>

void interleave(const uint8_t *in, uint8_t *out, size_t n, int depth);
void deinterleave(const uint8_t *in, uint8_t *out, size_t n, int depth);

#endif /* INTERLEAVE_H */
