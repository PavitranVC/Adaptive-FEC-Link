#include <string.h>
#include "sender.h"

static const char *const NAMES[] = {"fec", "arq", "harq", "adaptive"};

const char *strategy_name(strategy_t s) {
    return (s >= STRAT_FEC && s <= STRAT_ADAPTIVE) ? NAMES[s] : "?";
}

int strategy_from_name(const char *name, strategy_t *out) {
    for (int i = 0; i < 4; i++)
        if (!strcmp(name, NAMES[i])) { *out = (strategy_t)i; return 0; }
    return -1;
}

void sender_init(sender_t *s, strategy_t strategy, const fec_config_t *fixed,
                 const adapt_params_t *ap, int max_retries) {
    memset(s, 0, sizeof *s);
    s->strategy = strategy;
    s->fixed = *fixed;
    s->max_retries = max_retries;
    adapt_params_t defaults;
    adapt_defaults(&defaults);
    adapt_init(&s->ctl, ap ? ap : &defaults);
}

void sender_begin_frame(sender_t *s, uint32_t seq) {
    s->seq = seq;
    s->attempt = 0;
}

fec_config_t sender_config(const sender_t *s) {
    switch (s->strategy) {
    case STRAT_ARQ:      return fec_config(CODE_NONE);          /* CRC only */
    case STRAT_ADAPTIVE: return ladder_config(s->ctl.level);
    default:             return s->fixed;
    }
}

int sender_needs_feedback(const sender_t *s) { return s->strategy != STRAT_FEC; }

/* One more transmission, or give up when the retry budget is spent. */
static send_action_t retry_or_give_up(sender_t *s) {
    if (s->attempt >= s->max_retries) { s->gave_up++; return SEND_GIVE_UP; }
    s->attempt++;
    s->retransmissions++;
    return SEND_RETRY;
}

send_action_t sender_on_feedback(sender_t *s, const feedback_t *fb, int *level_change) {
    if (level_change) *level_change = 0;
    if (fb->seq != s->seq) return SEND_IGNORE;                  /* stale / duplicate */
    if (s->strategy == STRAT_ADAPTIVE) {
        int ch = adapt_update(&s->ctl, fb);                     /* may step the code level */
        if (level_change) *level_change = ch;
    }
    if (fb->type == FB_ACK) return SEND_DONE;
    if (fb->attempt != s->attempt) return SEND_IGNORE;          /* NAK of an older attempt */
    return retry_or_give_up(s);
}

send_action_t sender_on_timeout(sender_t *s) {
    s->timeouts++;
    return retry_or_give_up(s);
}
