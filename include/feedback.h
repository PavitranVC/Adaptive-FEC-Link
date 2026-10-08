/*
 * feedback.h - tollgate -> vehicle feedback on UDP port 9002 (stop-and-wait ARQ, HARQ, adaptive).
 *
 *  off size field
 *   0   2   magic "FB"
 *   2   1   version (1)
 *   3   1   type: 1 = ACK (frame delivered), 2 = NAK (CRC failed, please retransmit)
 *   4   4   seq of the frame this answers (big-endian)
 *   8   1   attempt of the transmission this answers
 *   9   1   ladder level of that transmission (255 = not a ladder code)
 *  10   2   win_n         frames in the receiver's sliding window      (adaptive controller)
 *  12   2   win_fail      of which failed the CRC
 *  14   4   win_corrected bits corrected in the successful frames of the window
 *  18   4   win_bits      coded bits of the successful frames of the window
 *
 * Feedback is never corrupted (like the header), but it can be LOST: the sender then times out.
 */
#ifndef FEEDBACK_H
#define FEEDBACK_H

#include <stddef.h>
#include <stdint.h>

#define FEEDBACK_BYTES 22
enum { FB_ACK = 1, FB_NAK = 2 };

typedef struct {
    uint8_t type;
    uint32_t seq;
    uint8_t attempt;
    uint8_t level;
    uint16_t win_n, win_fail;
    uint32_t win_corrected, win_bits;
} feedback_t;

/* Returns FEEDBACK_BYTES, or 0 if cap is too small. */
size_t feedback_serialize(const feedback_t *f, uint8_t *buf, size_t cap);
/* Returns 0 on success, -1 if malformed. */
int feedback_parse(const uint8_t *buf, size_t len, feedback_t *f);

#endif /* FEEDBACK_H */
