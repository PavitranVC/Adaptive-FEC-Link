/*
 * feedback.c - serialisation of the 22-byte tollgate -> vehicle ACK/NAK datagram (layout in
 * feedback.h). All multi-byte fields are big-endian.
 */
#include <string.h>
#include "adapt.h"
#include "feedback.h"

static void put32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
}
static uint32_t get32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

size_t feedback_serialize(const feedback_t *f, uint8_t *b, size_t cap) {
    if (cap < FEEDBACK_BYTES) return 0;
    b[0] = 'F'; b[1] = 'B'; b[2] = 1; b[3] = f->type;
    put32(b + 4, f->seq);
    b[8] = f->attempt;
    b[9] = f->level;
    b[10] = (uint8_t)(f->win_n >> 8); b[11] = (uint8_t)f->win_n;
    b[12] = (uint8_t)(f->win_fail >> 8); b[13] = (uint8_t)f->win_fail;
    put32(b + 14, f->win_corrected);
    put32(b + 18, f->win_bits);
    return FEEDBACK_BYTES;
}

/* Accepts only an exact 22-byte datagram with magic "FB", version 1 and type ACK or NAK, whose
 * level is a ladder level or FEEDBACK_NO_LEVEL and whose window counters are consistent
 * (win_n <= FEEDBACK_MAX_WINDOW, win_fail <= win_n). f is not modified on failure. */
int feedback_parse(const uint8_t *b, size_t len, feedback_t *f) {
    if (len != FEEDBACK_BYTES || b[0] != 'F' || b[1] != 'B' || b[2] != 1) return -1;
    if (b[3] != FB_ACK && b[3] != FB_NAK) return -1;
    /* Rejects a level beyond the ladder and win_fail > win_n or an absurd win_n: such a window
     * would give a failure rate above 1 and force the adaptive controller to step up. */
    unsigned level = b[9], win_n = (unsigned)((b[10] << 8) | b[11]), win_fail = (unsigned)((b[12] << 8) | b[13]);
    if ((level != FEEDBACK_NO_LEVEL && level >= (unsigned)ladder_levels()) ||
        win_n > FEEDBACK_MAX_WINDOW || win_fail > win_n)
        return -1;
    memset(f, 0, sizeof *f);
    f->type = b[3];
    f->seq = get32(b + 4);
    f->attempt = b[8];
    f->level = b[9];
    f->win_n = (uint16_t)((b[10] << 8) | b[11]);
    f->win_fail = (uint16_t)((b[12] << 8) | b[13]);
    f->win_corrected = get32(b + 14);
    f->win_bits = get32(b + 18);
    return 0;
}
