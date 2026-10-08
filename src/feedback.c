#include <string.h>
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

int feedback_parse(const uint8_t *b, size_t len, feedback_t *f) {
    if (len != FEEDBACK_BYTES || b[0] != 'F' || b[1] != 'B' || b[2] != 1) return -1;
    if (b[3] != FB_ACK && b[3] != FB_NAK) return -1;
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
