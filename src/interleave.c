/*
 * interleave.c - block interleaver between FEC encode and the channel (and its inverse after the
 * channel). Operates on whole coded bit vectors, one bit per byte; n is a bit count.
 */
#include "interleave.h"

/* in and out must not overlap. depth < 1 is treated as 1 (identity), and depth >= n is also the
 * identity because there is a single column. When depth does not divide n, the empty cells are
 * the trailing ones in row-major order and can span more than one row (n = 5, depth = 4: row 2
 * has one cell, row 3 none), hence the index test on every cell.
 * Transmission order = read the depth x cols matrix column by column, skipping the empty
 * cells at the end of the last row. Codeword index of cell (row r, column c) = r * cols + c. */

void interleave(const uint8_t *in, uint8_t *out, size_t n, int depth) {
    size_t d = depth < 1 ? 1 : (size_t)depth, cols = (n + d - 1) / d, sent = 0;
    for (size_t c = 0; c < cols; c++)
        for (size_t r = 0; r < d; r++)
            if (r * cols + c < n) out[sent++] = in[r * cols + c];
}

void deinterleave(const uint8_t *in, uint8_t *out, size_t n, int depth) {
    size_t d = depth < 1 ? 1 : (size_t)depth, cols = (n + d - 1) / d, sent = 0;
    for (size_t c = 0; c < cols; c++)
        for (size_t r = 0; r < d; r++)
            if (r * cols + c < n) out[r * cols + c] = in[sent++];
}
