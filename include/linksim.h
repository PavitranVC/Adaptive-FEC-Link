/*
 * linksim.h - in-process simulation of the whole link WITH retransmission strategies and a
 * simulated clock (no sockets, no sleeping): used by bin/bench and the unit tests.
 *
 * Time model for one transmission of n coded bits (all in milliseconds):
 *     airtime   = n / bitrate_kbps                 (kbit/s = bit/ms)
 *     received  = start + airtime + rtt/2 + decode   (decode = real measured decoder time)
 *     feedback  = received + rtt/2                 (ACK/NAK reaches the vehicle)
 *     if the feedback is lost (probability fb_drop) the vehicle waits until start + timeout.
 * Delivery latency of a frame = time from its FIRST transmission until the receiver holds a
 * CRC-valid copy. Frames never delivered count as failures, not in the latency statistics.
 *
 * The channel may follow a schedule of segments ("toll for 200 transmissions, then hospital..."),
 * counted in transmissions (air time), so retransmissions also consume the schedule.
 */
#ifndef LINKSIM_H
#define LINKSIM_H

#include <stdint.h>
#include <stdio.h>
#include "adapt.h"
#include "noise.h"
#include "sender.h"

#define LINKSIM_MAX_SEG 16

typedef struct {
    noise_params_t noise;
    long frames;              /* transmissions in this segment; the last segment lasts forever */
    const char *label;        /* e.g. "TOLL_PLAZA", for traces */
} linksim_seg_t;

typedef struct {
    strategy_t strategy;
    fec_config_t fixed;       /* code for fec / harq */
    adapt_params_t adapt;     /* controller + receiver window size (adapt.window) */
    int max_retries;          /* default 4 */
    double rtt_ms;            /* default 20 */
    double timeout_ms;        /* default 100 */
    double bitrate_kbps;      /* default 160 (a typical RFID tag-to-reader rate) */
    double fb_drop;           /* probability a feedback packet is lost, default 0 */
    long frames;
    uint64_t seed;
    int nseg;
    linksim_seg_t seg[LINKSIM_MAX_SEG];
} linksim_params_t;

typedef struct {
    long frames, correct, silent_wrong, failed;
    long transmissions, timeouts, level_changes;
    double success_rate, silent_wrong_rate, retx_per_frame;
    double mean_latency_ms, p99_latency_ms;
    double mean_code_rate;    /* average over transmissions of 128 / coded bits */
    double mean_level;        /* average ladder level over transmissions (-1 if not on ladder) */
} linksim_result_t;

void linksim_defaults(linksim_params_t *p);

/* trace (may be NULL): CSV rows  frame,tx,segment,level,attempts,class,latency_ms */
void linksim_run(const linksim_params_t *p, linksim_result_t *r, FILE *trace);

#endif /* LINKSIM_H */
