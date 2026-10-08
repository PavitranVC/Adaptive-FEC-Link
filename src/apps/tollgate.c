/*
 * tollgate - the receiver (roadside unit).
 *
 * For each frame from the channel (--tollgate-port, 9001): FEC-decode the coded bits, check the
 * CRC-32 of the recovered tag ID, and classify the frame:
 *     CORRECT        CRC ok and the ID matches ground truth
 *     DETECTED_FAIL  CRC failed / decoder detected an uncorrectable block -> reject the read
 *     SILENT_WRONG   CRC ok but the ID is wrong (ground truth from the channel's sidecar)
 * Per frame it logs: seq, code, bits flipped (truth), bits corrected, CRC result, class,
 * decode latency (FEC decode + CRC check, microseconds).
 *
 * Week 2: when a frame's header says strategy != fec, the tollgate answers every transmission
 * with ACK (it now holds a CRC-valid copy of that seq) or NAK (CRC failed) on the feedback port
 * (9002), after a simulated return propagation of rtt/2. The feedback also carries the sliding
 * window statistics used by the adaptive controller (window.h). Retransmissions of the same seq
 * are decoded again; a frame's final class is that of its first accepted copy (or DETECTED_FAIL
 * if no copy passed). Duplicates (same seq and attempt) are ignored.
 */
#include <stdio.h>
#include <string.h>
#include "bits.h"
#include "adapt.h"
#include "cli.h"
#include "feedback.h"
#include "fec.h"
#include "frame.h"
#include "netutil.h"
#include "noise.h"
#include "packet.h"
#include "profiles.h"
#include "receiver.h"
#include "term.h"
#include "window.h"

#define TAG "TOLLGATE"
#define COL T_CYAN

typedef struct {
    long sent;                 /* from the END packet, -1 if unknown */
    long received, duplicates; /* received = distinct frames (seq numbers) */
    long transmissions;        /* every decoded transmission, incl. retransmissions */
    long cls[4];               /* final class per distinct frame */
    int strategy;              /* strategy seen in the headers */
    /* the frame currently being (re)transmitted */
    int cur_valid, cur_delivered, cur_attempt;
    uint32_t cur_seq;
    frame_class_t cur_cls;
    /* feedback */
    int fbfd;
    window_t win;
    rng_t fb_rng;
    long fb_sent, fb_dropped;
    unsigned long long flipped, corrected;
    double lat_sum, lat_max;
    int code, rs_t, interleave, nbits;
    int profile, model;        /* from the ground-truth sidecar, -1 unknown */
} stats_t;

typedef struct {
    uint32_t seq;
    int attempt;
    int flipped, corrected, crc_ok;
    frame_class_t cls;
    double lat_us;
    char tag_true[TAG_HEX_LEN + 1], tag_rx[TAG_HEX_LEN + 1];
} frame_report_t;

static term_color_t class_color(frame_class_t c) {
    return c == CLASS_CORRECT ? T_GREEN : c == CLASS_SILENT_WRONG ? T_RED : T_YELLOW;
}

/* Bit map of one frame, grouped per code block:
 *   repaired (flipped by channel, fixed by decoder)      green   / 'C'
 *   residual (flipped, not fixed)                        red     / 'X'
 *   miscorrected (not flipped, changed by the decoder)   magenta / 'M'
 *   untouched                                            dim     / '.'          */
static void print_bitmap(const packet_t *p, const uint8_t *rx, const uint8_t *fixed, int block_n) {
    int group = block_n <= 16 ? block_n : 8;
    int per_line = 96 / (group + 1);
    if (per_line < 1) per_line = 1;
    char line[4096];  /* worst case: 96 bits x ~14 bytes of colour codes */
    size_t len = 0;
    int groups_on_line = 0;
    for (int i = 0; i < p->nbits; i++) {
        int flipped = p->has_truth && rx[i] != p->truth.clean_bits[i];
        int changed = rx[i] != fixed[i];
        term_color_t c = T_DIM;
        char sym = '.';
        if (flipped && changed) { c = T_BG_GREEN; sym = 'C'; }
        else if (flipped)       { c = T_BG_RED; sym = 'X'; }
        else if (changed)       { c = T_BG_MAGENTA; sym = 'M'; }
        if (len + 32 >= sizeof line) break;  /* never overflow, even for odd widths */
        if (term_enabled())
            len += (size_t)snprintf(line + len, sizeof line - len, "%s%c%s", term_c(c),
                                    rx[i] ? '1' : '0', term_c(T_RESET));
        else
            line[len++] = sym;
        line[len] = '\0';
        if ((i + 1) % group == 0 || i + 1 == p->nbits) {
            if (++groups_on_line == per_line || i + 1 == p->nbits) {
                term_say(TAG, COL, "    %s", line);
                len = 0;
                line[0] = '\0';
                groups_on_line = 0;
            } else {
                line[len++] = ' ';
                line[len] = '\0';
            }
        }
    }
}

static void print_legend(void) {
    if (term_enabled())
        term_say(TAG, COL, "bit map legend: %s1%s repaired  %s1%s residual error  %s1%s miscorrected  %s0%s untouched",
                 term_c(T_BG_GREEN), term_c(T_RESET), term_c(T_BG_RED), term_c(T_RESET),
                 term_c(T_BG_MAGENTA), term_c(T_RESET), term_c(T_DIM), term_c(T_RESET));
    else
        term_say(TAG, COL, "bit map legend: C repaired  X residual error  M miscorrected  . untouched");
}

static void print_report(const frame_report_t *r, const char *code) {
    char flipped[16];
    if (r->flipped >= 0) snprintf(flipped, sizeof flipped, "%2d", r->flipped);
    else snprintf(flipped, sizeof flipped, " ?");
    char seq[24];
    if (r->attempt > 0) snprintf(seq, sizeof seq, "%u#%d", r->seq, r->attempt);
    else snprintf(seq, sizeof seq, "%u", r->seq);
    term_say(TAG, COL, "seq=%-4s code=%-11s flipped=%s corrected=%2d crc=%s%-4s%s %s%s%-13s%s tag=%s lat=%6.2fus",
             seq, code, flipped, r->corrected,
             r->crc_ok ? term_c(T_GREEN) : term_c(T_RED), r->crc_ok ? "OK" : "FAIL", term_c(T_RESET),
             term_c(T_BOLD), term_c(class_color(r->cls)), frame_class_name(r->cls), term_c(T_RESET),
             r->tag_rx, r->lat_us);
}

/* Decodes and reports one transmission; returns its class, or -1 if it was dropped. */
static int handle_data(const cli_opts_t *o, packet_t *p, stats_t *st, FILE *log, rx_decode_t *dout) {
    uint8_t rx[PACKET_MAX_BITS];
    rx_decode_t d;
    /* ---- decode: sees ONLY the header and the received bits (never p->truth) ---- */
    if (rx_decode_packet(p, &d) != 0) {
        term_say(TAG, COL, "dropping frame seq=%u with unknown code/size", p->seq);
        return -1;
    }
    memcpy(rx, p->bits, p->nbits);
    fec_config_t fc = d.cfg;

    /* ---- evaluation: classification against the channel's ground truth ---- */
    frame_report_t r;
    r.seq = p->seq;
    r.attempt = p->attempt;
    r.flipped = p->has_truth ? p->truth.flips : -1;
    r.corrected = d.corrected_bits;
    r.crc_ok = d.crc_ok;
    r.cls = rx_classify(&d, p);
    r.lat_us = d.decode_us;
    double lat = d.decode_us;
    tag_to_hex(d.id, r.tag_rx);
    if (p->has_truth) tag_to_hex(p->truth.tag, r.tag_true);
    else strcpy(r.tag_true, "?");

    st->transmissions++;
    st->flipped += r.flipped > 0 ? (unsigned)r.flipped : 0;
    st->corrected += (unsigned)r.corrected;
    st->lat_sum += lat;
    if (lat > st->lat_max) st->lat_max = lat;
    st->code = fc.code; st->rs_t = fc.rs_t; st->interleave = fc.interleave; st->nbits = p->nbits;
    if (p->has_truth) { st->profile = p->truth.profile; st->model = p->truth.model; }

    if (!o->quiet) {
        print_report(&r, fec_code_name(fc.code));
        if (o->show_bits && (r.flipped > 0 || r.corrected > 0))
            print_bitmap(p, rx, d.repaired, fec_block_n(&fc));
    }
    if (log)
        fprintf(log, "%u,%s,%d,%d,%d,%s,%.3f,%s,%s\n", r.seq, fec_code_name(fc.code), r.flipped,
                r.corrected, r.crc_ok, frame_class_name(r.cls), r.lat_us, r.tag_true, r.tag_rx);
    *dout = d;
    return (int)r.cls;
}

/* ACK if we hold a CRC-valid copy of this seq (now or from an earlier attempt), else NAK. */
static void send_feedback(const cli_opts_t *o, stats_t *st, const packet_t *p, const rx_decode_t *d) {
    feedback_t fb;
    uint8_t wire[FEEDBACK_BYTES];
    memset(&fb, 0, sizeof fb);
    int lvl = ladder_level_of(&d->cfg);
    window_push(&st->win, lvl, !rx_accept(d), d->corrected_bits, p->nbits);
    window_fill_feedback(&st->win, &fb);
    fb.type = st->cur_delivered ? FB_ACK : FB_NAK;
    fb.seq = p->seq;
    fb.attempt = p->attempt;
    fb.level = (uint8_t)(lvl < 0 ? 255 : lvl);
    if (o->rtt_ms > 0) sleep_us(o->rtt_ms * 500L);              /* return propagation */
    if (rng_bernoulli(&st->fb_rng, o->fb_drop)) {                /* failure injection */
        st->fb_dropped++;
        if (!o->quiet) term_say(TAG, COL, "   %s(feedback for seq=%u dropped - injected loss)%s",
                                term_c(T_YELLOW), p->seq, term_c(T_RESET));
        return;
    }
    feedback_serialize(&fb, wire, sizeof wire);
    net_sendto(st->fbfd, o->host, o->feedback_port, wire, sizeof wire);
    st->fb_sent++;
    if (!o->quiet)
        term_say(TAG, COL, "   -> %s%s%s seq=%u  (window: %u frames, %u failed, BER^ %.4f)",
                 fb.type == FB_ACK ? term_c(T_GREEN) : term_c(T_YELLOW),
                 fb.type == FB_ACK ? "ACK" : "NAK", term_c(T_RESET), fb.seq, fb.win_n, fb.win_fail,
                 fb.win_bits ? (double)fb.win_corrected / fb.win_bits : 0.0);
}

/* The previous seq is complete: count its final class once. */
static void finalize_frame(stats_t *st) {
    if (!st->cur_valid) return;
    st->received++;
    st->cls[st->cur_cls]++;
    st->cur_valid = 0;
}

static double pct(long a, long b) { return b > 0 ? 100.0 * (double)a / (double)b : 0.0; }

static void print_summary(const stats_t *st) {
    long lost = st->sent >= 0 ? st->sent - st->received : 0;
    const profile_t *prof = profile_by_id(st->profile);
    fec_config_t fc = fec_config((code_id_t)st->code);
    fc.rs_t = st->rs_t;
    fc.interleave = st->interleave;
    char desc[160] = "?";
    if (st->received > 0) fec_describe(&fc, desc, sizeof desc);
    term_say(TAG, COL, "%s==================== SUMMARY ====================%s", term_c(T_BOLD), term_c(T_RESET));
    term_say(TAG, COL, "profile         : %s, noise model %s", prof ? prof->label : "?",
             st->model >= 0 ? noise_model_name((noise_model_t)st->model) : "?");
    term_say(TAG, COL, "code            : %s", desc);
    if (st->sent >= 0)
        term_say(TAG, COL, "frames          : sent %ld, received %ld, lost %ld", st->sent, st->received, lost);
    else
        term_say(TAG, COL, "frames          : received %ld (END not seen)", st->received);
    term_say(TAG, COL, "%sCORRECT        %s : %5ld  (%5.1f%%)", term_c(T_GREEN), term_c(T_RESET),
             st->cls[CLASS_CORRECT], pct(st->cls[CLASS_CORRECT], st->received));
    term_say(TAG, COL, "%sDETECTED_FAIL  %s : %5ld  (%5.1f%%)", term_c(T_YELLOW), term_c(T_RESET),
             st->cls[CLASS_DETECTED_FAIL], pct(st->cls[CLASS_DETECTED_FAIL], st->received));
    term_say(TAG, COL, "%sSILENT_WRONG   %s : %5ld  (%5.1f%%)", term_c(T_RED), term_c(T_RESET),
             st->cls[CLASS_SILENT_WRONG], pct(st->cls[CLASS_SILENT_WRONG], st->received));
    if (st->cls[CLASS_UNVERIFIED])
        term_say(TAG, COL, "UNVERIFIED      : %5ld  (no ground truth)", st->cls[CLASS_UNVERIFIED]);
    term_say(TAG, COL, "bits            : flipped %llu, corrected %llu", st->flipped, st->corrected);
    term_say(TAG, COL, "decode latency  : mean %.2f us, max %.2f us",
             st->transmissions ? st->lat_sum / (double)st->transmissions : 0.0, st->lat_max);
    if (st->strategy > STRAT_FEC)
        term_say(TAG, COL, "strategy %-7s: %ld transmissions (%ld retransmissions), %ld feedback sent, %ld dropped (injected), %ld duplicates ignored",
                 strategy_name((strategy_t)st->strategy), st->transmissions,
                 st->transmissions - st->received, st->fb_sent, st->fb_dropped, st->duplicates);
}

static void write_summary(const char *path, const stats_t *st) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    const profile_t *prof = profile_by_id(st->profile);
    fec_config_t fc = fec_config((code_id_t)st->code);
    fc.rs_t = st->rs_t;
    fc.interleave = st->interleave;
    long n = st->received;
    fprintf(f, "code=%s\n", st->received ? fec_code_name(fc.code) : "?");
    fprintf(f, "profile=%s\n", prof ? prof->label : "?");
    fprintf(f, "model=%s\n", st->model >= 0 ? noise_model_name((noise_model_t)st->model) : "?");
    fprintf(f, "coded_bits=%d\n", st->nbits);
    fprintf(f, "code_rate=%.3f\n", st->received ? fec_code_rate(&fc, PAYLOAD_BITS) : 0.0);
    fprintf(f, "sent=%ld\n", st->sent);
    fprintf(f, "received=%ld\n", n);
    fprintf(f, "lost=%ld\n", st->sent >= 0 ? st->sent - n : 0);
    fprintf(f, "correct=%ld\n", st->cls[CLASS_CORRECT]);
    fprintf(f, "detected_fail=%ld\n", st->cls[CLASS_DETECTED_FAIL]);
    fprintf(f, "silent_wrong=%ld\n", st->cls[CLASS_SILENT_WRONG]);
    fprintf(f, "success_rate=%.4f\n", n ? (double)st->cls[CLASS_CORRECT] / (double)n : 0.0);
    fprintf(f, "bits_flipped=%llu\n", st->flipped);
    fprintf(f, "bits_corrected=%llu\n", st->corrected);
    fprintf(f, "mean_decode_us=%.3f\n", st->transmissions ? st->lat_sum / (double)st->transmissions : 0.0);
    fprintf(f, "max_decode_us=%.3f\n", st->lat_max);
    if (st->strategy > STRAT_FEC) {
        fprintf(f, "strategy=%s\n", strategy_name((strategy_t)st->strategy));
        fprintf(f, "transmissions=%ld\n", st->transmissions);
        fprintf(f, "retransmissions=%ld\n", st->transmissions - n);
        fprintf(f, "feedback_dropped=%ld\n", st->fb_dropped);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    static packet_t pkt;
    static uint8_t wire[PACKET_MAX_BYTES];
    static uint8_t seen[1 << 16];  /* duplicate detection by seq (mod 65536) */
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    term_init(o.color);
    install_stop_handler();

    int fd = net_udp_bind(o.host, o.tollgate_port);
    if (fd < 0) return 1;
    FILE *log = o.log_path ? fopen(o.log_path, "w") : NULL;
    if (log) fprintf(log, "seq,code,flipped,corrected,crc_ok,class,latency_us,tag_true,tag_rx\n");

    term_say(TAG, COL, "listening on %s:%d (ACK/NAK feedback to port %d when strategy != fec)",
             o.host, o.tollgate_port, o.feedback_port);
    if (o.show_bits && !o.quiet) print_legend();

    stats_t st;
    memset(&st, 0, sizeof st);
    st.sent = -1;
    st.profile = st.model = -1;
    st.fbfd = net_udp_socket();
    window_init(&st.win, o.window);
    rng_seed(&st.fb_rng, rng_derive(o.seed, "tollgate-fbdrop"));
    int got_first = 0;
    while (!stop_requested()) {
        long n = net_recv(fd, wire, sizeof wire, got_first && o.idle_timeout_ms ? o.idle_timeout_ms : -1);
        if (n == 0) { term_say(TAG, COL, "idle timeout - stopping"); break; }
        if (n < 0) continue;
        if (packet_parse(wire, (size_t)n, &pkt) != 0) {
            term_say(TAG, COL, "ignoring malformed packet (%ld bytes)", n);
            continue;
        }
        got_first = 1;
        if (pkt.type == PKT_END) { st.sent = pkt.seq; break; }
        if (st.cur_valid && pkt.seq == st.cur_seq) {
            if (pkt.attempt <= st.cur_attempt) { st.duplicates++; continue; }  /* same copy again */
        } else if (seen[pkt.seq & 0xFFFF] && pkt.seq < 0x10000) {
            st.duplicates++;                                              /* old frame, late copy */
            continue;
        } else {                                                          /* a new frame */
            finalize_frame(&st);
            seen[pkt.seq & 0xFFFF] = 1;
            st.cur_valid = 1;
            st.cur_seq = pkt.seq;
            st.cur_delivered = 0;
            st.cur_cls = CLASS_DETECTED_FAIL;
        }
        st.cur_attempt = pkt.attempt;
        st.strategy = pkt.strategy;
        rx_decode_t d;
        int cls = handle_data(&o, &pkt, &st, log, &d);
        if (cls < 0) continue;
        if (!st.cur_delivered) {                     /* first accepted copy decides the class */
            st.cur_cls = (frame_class_t)cls;
            st.cur_delivered = rx_accept(&d);
        }
        if (pkt.strategy != STRAT_FEC) send_feedback(&o, &st, &pkt, &d);
    }
    finalize_frame(&st);
    print_summary(&st);
    if (o.summary_path) write_summary(o.summary_path, &st);
    if (log) fclose(log);
    net_close(fd);
    net_close(st.fbfd);
    return 0;
}
