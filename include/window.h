/*
 * window.h - the receiver's sliding window over the last W transmissions AT ONE CODE LEVEL.
 *
 * For every decoded transmission the tollgate pushes (failed?, corrected bits, coded bits).
 * The window keeps running sums so that the feedback can report, over the last W frames:
 *     win_n          frames in the window (<= W)
 *     win_fail       frames whose CRC failed after decoding
 *     win_corrected  bits the decoder corrected in the SUCCESSFUL frames
 *     win_bits       coded bits of the SUCCESSFUL frames
 * so the vehicle can estimate the failure rate  f = win_fail / win_n  and the channel bit error
 * rate  BER^ = win_corrected / win_bits. When a frame arrives with a different code level the
 * window is cleared: evidence about one code says little about another.
 */
#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include "feedback.h"

#define WINDOW_MAX 1024

typedef struct {
    int size, count, head, level;
    uint8_t failed[WINDOW_MAX];
    uint32_t corrected[WINDOW_MAX], bits[WINDOW_MAX];
    long sum_fail, sum_corrected, sum_bits;
} window_t;

void window_init(window_t *w, int size);   /* size is clamped to 1..WINDOW_MAX */
void window_reset(window_t *w, int level);
void window_push(window_t *w, int level, int failed, int corrected, int bits);
void window_fill_feedback(const window_t *w, feedback_t *fb);  /* sets the win_* fields */

#endif /* WINDOW_H */
