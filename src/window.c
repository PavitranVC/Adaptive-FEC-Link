#include <string.h>
#include "window.h"

void window_init(window_t *w, int size) {
    memset(w, 0, sizeof *w);
    w->size = size < 1 ? 1 : size > WINDOW_MAX ? WINDOW_MAX : size;
    w->level = -1;
}

void window_reset(window_t *w, int level) {
    int size = w->size;
    window_init(w, size);
    w->level = level;
}

/* Ring buffer: `head` is the oldest entry, entries head .. head+count-1 (mod size) are live. */
void window_push(window_t *w, int level, int failed, int corrected, int bits) {
    if (level != w->level) window_reset(w, level);
    if (w->count == w->size) {               /* full: evict the oldest entry */
        int old = w->head;
        w->sum_fail -= w->failed[old];
        w->sum_corrected -= w->corrected[old];
        w->sum_bits -= w->bits[old];
        w->head = (w->head + 1) % w->size;
        w->count--;
    }
    int slot = (w->head + w->count) % w->size;
    w->failed[slot] = (uint8_t)(failed != 0);
    w->corrected[slot] = failed ? 0u : (uint32_t)corrected;   /* successful frames only */
    w->bits[slot] = failed ? 0u : (uint32_t)bits;
    w->sum_fail += w->failed[slot];
    w->sum_corrected += w->corrected[slot];
    w->sum_bits += w->bits[slot];
    w->count++;
}

void window_fill_feedback(const window_t *w, feedback_t *fb) {
    fb->win_n = (uint16_t)w->count;
    fb->win_fail = (uint16_t)w->sum_fail;
    fb->win_corrected = (uint32_t)w->sum_corrected;
    fb->win_bits = (uint32_t)w->sum_bits;
}
