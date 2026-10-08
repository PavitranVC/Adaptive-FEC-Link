/*
 * vehicle - the sender (the car's RFID tag + transponder).
 *
 * For each frame: draw a random 96-bit tag ID (seeded), append CRC-32, FEC-encode the 128-bit
 * payload and send it over UDP to the channel (127.0.0.1:9000 by default).
 *
 * --strategy fec (default)  send once, no feedback (week-1 behaviour, unchanged)
 * --strategy arq|harq|adaptive
 *     stop-and-wait: after each transmission wait for ACK/NAK from the tollgate on the feedback
 *     port (9002) for at most --timeout-ms; retransmit on NAK/timeout up to --max-retries.
 *     The round trip is simulated: the vehicle waits rtt/2 before each transmission (forward
 *     propagation) and the tollgate waits rtt/2 before answering (return propagation).
 *     adaptive: every feedback carries the receiver's window statistics; the controller in
 *     sender_t may step the code level up or down (printed as "LEVEL UP/DOWN").
 *
 * After --count frames it sends an END packet carrying the number of frames sent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "fec.h"
#include "feedback.h"
#include "frame.h"
#include "netutil.h"
#include "packet.h"
#include "sender.h"
#include "term.h"

#define TAG "VEHICLE"

typedef struct {
    long sent, delivered, gave_up;
    double *lat_ms;            /* delivery latency per delivered frame */
} vstats_t;

static int send_packet(int fd, const cli_opts_t *o, const packet_t *p) {
    uint8_t wire[PACKET_MAX_BYTES];
    size_t len = packet_serialize(p, wire, sizeof wire);
    if (len == 0 || net_sendto(fd, o->host, o->channel_port, wire, len) != 0) {
        term_say(TAG, T_BLUE, "%serror:%s send failed", term_c(T_RED), term_c(T_RESET));
        return -1;
    }
    return 0;
}

static void send_end(int fd, const cli_opts_t *o, uint32_t sent) {
    packet_t p;
    memset(&p, 0, sizeof p);
    p.type = PKT_END;
    p.seq = sent;
    p.strategy = (uint8_t)o->strategy;
    /* sent twice: UDP gives no delivery guarantee; receivers ignore the duplicate */
    send_packet(fd, o, &p);
    sleep_us(20000);
    send_packet(fd, o, &p);
}

static void announce_level(const sender_t *s, int change) {
    const adapt_t *a = &s->ctl;
    if (change > 0)
        term_say(TAG, T_BLUE, "%s%s*** LEVEL UP   -> %s   (window failure rate %.0f%% > up-threshold %.0f%%) ***%s",
                 term_c(T_BOLD), term_c(T_RED), ladder_label(a->level), 100 * a->last_fail,
                 100 * a->p.up_threshold, term_c(T_RESET));
    else if (change < 0)
        term_say(TAG, T_BLUE, "%s%s*** LEVEL DOWN -> %s   (BER^ %.4f, predicted failure %.1f%% < down-threshold %.0f%%) ***%s",
                 term_c(T_BOLD), term_c(T_GREEN), ladder_label(a->level), a->last_ber,
                 100 * a->last_pred, 100 * a->p.down_threshold, term_c(T_RESET));
}

/* Waits for the answer to the current transmission. Returns the sender's decision. */
static send_action_t await_feedback(int fbfd, const cli_opts_t *o, sender_t *s, double t_sent) {
    uint8_t buf[64];
    for (;;) {
        int remaining = o->timeout_ms - (int)((now_us() - t_sent) / 1000.0);
        long n = remaining > 0 ? net_recv(fbfd, buf, sizeof buf, remaining) : 0;
        if (stop_requested()) return SEND_GIVE_UP;
        if (n == 0) {
            if (!o->quiet) term_say(TAG, T_BLUE, "seq=%-4u %stimeout%s (no feedback in %d ms)",
                                    s->seq, term_c(T_YELLOW), term_c(T_RESET), o->timeout_ms);
            return sender_on_timeout(s);
        }
        feedback_t fb;
        if (n < 0 || feedback_parse(buf, (size_t)n, &fb) != 0) continue;
        int change = 0;
        send_action_t act = sender_on_feedback(s, &fb, &change);
        announce_level(s, change);
        if (act == SEND_IGNORE) {
            if (!o->quiet) term_say(TAG, T_BLUE, "ignoring stale feedback (seq=%u attempt=%u)", fb.seq, fb.attempt);
            continue;
        }
        if (!o->quiet && fb.type == FB_NAK)
            term_say(TAG, T_BLUE, "seq=%-4u %sNAK%s for attempt %u", fb.seq, term_c(T_YELLOW),
                     term_c(T_RESET), fb.attempt);
        return act;
    }
}

/* Sends one frame according to the strategy; returns 1 if delivered. */
static int send_frame(int fd, int fbfd, const cli_opts_t *o, sender_t *s, rng_t *rng, long seq,
                      vstats_t *vs) {
    static packet_t pkt;
    uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
    char hex[TAG_HEX_LEN + 1];
    tag_random(rng, id);
    frame_build_payload(id, payload);
    tag_to_hex(id, hex);
    sender_begin_frame(s, (uint32_t)seq);
    int fb = sender_needs_feedback(s);
    double t_first = now_us();

    for (;;) {
        fec_config_t fc = sender_config(s);
        memset(&pkt, 0, sizeof pkt);
        pkt.type = PKT_DATA;
        pkt.code = (uint8_t)fc.code;
        pkt.rs_t = (uint8_t)fc.rs_t;
        pkt.interleave = (uint8_t)fc.interleave;
        pkt.seq = (uint32_t)seq;
        pkt.attempt = (uint8_t)s->attempt;
        pkt.strategy = (uint8_t)s->strategy;
        pkt.nbits = (uint16_t)fec_encode(&fc, payload, PAYLOAD_BITS, pkt.bits);
        if (fb && o->rtt_ms > 0) sleep_us(o->rtt_ms * 500L);       /* forward propagation */
        if (send_packet(fd, o, &pkt)) return 0;
        double t_sent = now_us();
        vs->sent++;
        if (!o->quiet) {
            if (s->attempt == 0)
                term_say(TAG, T_BLUE, "seq=%-4ld tag=%s -> %u coded bits (%s)", seq, hex, pkt.nbits,
                         fec_code_name(fc.code));
            else
                term_say(TAG, T_BLUE, "seq=%-4ld %sretransmission #%d%s -> %u coded bits (%s)", seq,
                         term_c(T_YELLOW), s->attempt, term_c(T_RESET), pkt.nbits, fec_code_name(fc.code));
        }
        if (!fb) return 1;                                         /* plain FEC: fire and forget */

        send_action_t act = await_feedback(fbfd, o, s, t_sent);
        if (act == SEND_DONE) {
            /* delivery happened rtt/2 before the ACK reached us (simulated return trip) */
            double lat = (now_us() - t_first) / 1000.0 - o->rtt_ms / 2.0;
            vs->lat_ms[vs->delivered++] = lat;
            if (!o->quiet) term_say(TAG, T_BLUE, "seq=%-4ld %sACK%s after %d transmission(s), %.1f ms",
                                    seq, term_c(T_GREEN), term_c(T_RESET), s->attempt + 1, lat);
            return 1;
        }
        if (act == SEND_GIVE_UP) {
            vs->gave_up++;
            term_say(TAG, T_BLUE, "seq=%-4ld %sGIVING UP%s after %d transmission(s)", seq,
                     term_c(T_RED), term_c(T_RESET), s->attempt + 1);
            return 0;
        }
    }
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void print_summary(const cli_opts_t *o, const sender_t *s, vstats_t *vs, long frames) {
    term_say(TAG, T_BLUE, "done: %ld frames sent", frames);
    if (!sender_needs_feedback(s)) return;
    double mean = 0, p99 = 0;
    if (vs->delivered > 0) {
        for (long i = 0; i < vs->delivered; i++) mean += vs->lat_ms[i];
        mean /= vs->delivered;
        qsort(vs->lat_ms, (size_t)vs->delivered, sizeof(double), cmp_double);
        p99 = vs->lat_ms[(long)(0.99 * (vs->delivered - 1) + 0.5)];
    }
    term_say(TAG, T_BLUE, "strategy %s: %ld transmissions (%.2f retransmissions/frame), %ld ACKed, %ld given up, %ld timeouts",
             strategy_name(o->strategy), vs->sent, frames ? (double)(vs->sent - frames) / frames : 0.0,
             vs->delivered, vs->gave_up, s->timeouts);
    term_say(TAG, T_BLUE, "latency until ACK (minus return trip): mean %.1f ms, p99 %.1f ms (simulated RTT %d ms)", mean, p99, o->rtt_ms);
    if (o->strategy == STRAT_ADAPTIVE)
        term_say(TAG, T_BLUE, "adaptive: %ld level changes (%ld up, %ld down, %ld failed probes), final %s",
                 s->ctl.changes, s->ctl.ups, s->ctl.downs, s->ctl.failed_probes, ladder_label(s->ctl.level));
}

int main(int argc, char **argv) {
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    term_init(o.color);
    install_stop_handler();

    fec_config_t fc = cli_fec_config(&o);
    adapt_params_t ap = cli_adapt_params(&o);
    sender_t snd;
    sender_init(&snd, o.strategy, &fc, &ap, o.max_retries);
    fec_config_t first = sender_config(&snd);
    char desc[160];
    fec_describe(&first, desc, sizeof desc);
    int fd = net_udp_socket();
    if (fd < 0) return 1;
    int fbfd = -1;
    if (sender_needs_feedback(&snd) && (fbfd = net_udp_bind(o.host, o.feedback_port)) < 0) return 1;

    rng_t rng;
    rng_seed(&rng, rng_derive(o.seed, "vehicle"));
    term_say(TAG, T_BLUE, "sending %ld frames to channel %s:%d | code %s | seed %llu",
             o.count, o.host, o.channel_port, desc, (unsigned long long)o.seed);
    if (sender_needs_feedback(&snd))
        term_say(TAG, T_BLUE, "strategy %s: feedback on port %d, RTT %d ms, timeout %d ms, max %d retries%s",
                 strategy_name(o.strategy), o.feedback_port, o.rtt_ms, o.timeout_ms, o.max_retries,
                 o.strategy == STRAT_ADAPTIVE ? ", adaptive code ladder" : "");
    if (o.strategy == STRAT_ADAPTIVE)
        term_say(TAG, T_BLUE, "adaptive: start %s, window %d, up > %.0f%% failures, down < %.0f%% predicted",
                 ladder_label(snd.ctl.level), ap.window, 100 * ap.up_threshold, 100 * ap.down_threshold);

    vstats_t vs = {0, 0, 0, calloc((size_t)(o.count > 0 ? o.count : 1), sizeof(double))};
    long frames = 0;
    for (long seq = 0; seq < o.count && !stop_requested(); seq++) {
        send_frame(fd, fbfd, &o, &snd, &rng, seq, &vs);
        frames++;
        /* --delay-ms paces the demo; without it a 100 us gap keeps loopback buffers happy */
        sleep_us(o.delay_ms > 0 ? (long)o.delay_ms * 1000L : 100L);
    }
    print_summary(&o, &snd, &vs, frames);
    send_end(fd, &o, (uint32_t)frames);
    free(vs.lat_ms);
    net_close(fd);
    net_close(fbfd);
    return 0;
}
