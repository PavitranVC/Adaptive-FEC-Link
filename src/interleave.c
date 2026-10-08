#include "interleave.h"

/* Transmission order = read the depth x cols matrix column by column, skipping the empty
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
