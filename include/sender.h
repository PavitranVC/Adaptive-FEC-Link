/*
 * sender.h - the vehicle's transmission strategy as a small state machine, shared by the UDP
 * vehicle (bin/vehicle) and the in-process link simulator (bin/bench), so both run the same logic.
 *
 *  fec       send once with the fixed code, never wait for feedback (week-1 behaviour)
 *  arq       Stop-and-Wait ARQ: send UNCODED (CRC-32 only), wait for ACK/NAK;
 *            retransmit on NAK or timeout, at most max_retries times
 *  harq      Hybrid ARQ type I: send with the fixed FEC code, retransmit only when the receiver
 *            reports a CRC failure after decoding (NAK) or the feedback times out
 *  adaptive  HARQ + adaptive code level (see adapt.h), week-2 task 3
 *
 * Stop-and-wait rules for lost/duplicate feedback:
 *  - feedback for another seq          -> stale duplicate, ignored
 *  - ACK for the current seq (any attempt) -> delivered (a late ACK is still an ACK)
 *  - NAK for an older attempt          -> stale (we already retransmitted after a timeout), ignored
 *  - no feedback before the timeout    -> treated like a NAK
 */
#ifndef SENDER_H
#define SENDER_H

#include <stdint.h>
#include "adapt.h"
#include "fec.h"
#include "feedback.h"

typedef enum { STRAT_FEC = 0, STRAT_ARQ = 1, STRAT_HARQ = 2, STRAT_ADAPTIVE = 3 } strategy_t;
typedef enum { SEND_DONE, SEND_RETRY, SEND_GIVE_UP, SEND_IGNORE } send_action_t;

typedef struct {
    strategy_t strategy;
    fec_config_t fixed;     /* code for fec / harq */
    int max_retries;
    uint32_t seq;
    int attempt;            /* 0 = first transmission of the current frame */
    adapt_t ctl;            /* adaptive controller (strategy adaptive only) */
    long retransmissions, timeouts, gave_up;
} sender_t;

const char *strategy_name(strategy_t s);
int strategy_from_name(const char *name, strategy_t *out); /* 0 ok, -1 unknown */

/* ap may be NULL (defaults) - only used by STRAT_ADAPTIVE. */
void sender_init(sender_t *s, strategy_t strategy, const fec_config_t *fixed,
                 const adapt_params_t *ap, int max_retries);
void sender_begin_frame(sender_t *s, uint32_t seq);
fec_config_t sender_config(const sender_t *s);   /* code for the current transmission */
int sender_needs_feedback(const sender_t *s);

/* level_change (may be NULL) receives -1 / 0 / +1 when the adaptive controller moved. */
send_action_t sender_on_feedback(sender_t *s, const feedback_t *fb, int *level_change);
send_action_t sender_on_timeout(sender_t *s);

#endif /* SENDER_H */
