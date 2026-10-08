/*
 * channel - the noisy radio link (noise injector) between vehicle and tollgate.
 *
 * Receives coded frames on --channel-port (9000), flips bits in the coded payload according to
 * the selected noise model (header untouched), and forwards to --tollgate-port (9001).
 * It also acts as the evaluation "oracle": it knows the clean codeword and the true tag ID, and
 * appends them (plus the number of flipped bits) as a ground-truth sidecar. The tollgate uses
 * that ONLY to classify frames (SILENT_WRONG detection), never to decode.
 *
 * --schedule "toll:200,hospital:200,toll:200" switches the interference profile after the given
 * number of transmissions (retransmissions included); the last profile then stays in force.
 */
#include <stdio.h>
#include <string.h>
#include "bits.h"
#include "cli.h"
#include "fec.h"
#include "frame.h"
#include "netutil.h"
#include "noise.h"
#include "packet.h"
#include "schedule.h"
#include "term.h"

#define TAG "CHANNEL"
#define COL T_MAGENTA

typedef struct {
    long frames;
    unsigned long long bits, flips;
} channel_stats_t;

typedef struct {               /* the optional profile schedule */
    int active, seg;
    schedule_t sched;
} channel_sched_t;

/* Before transmission number tx: switch the noise to the scheduled profile if it changed. */
static const profile_t *apply_schedule(const cli_opts_t *o, channel_sched_t *cs, noise_t *ch, long tx) {
    if (!cs->active) return o->profile;
    int seg = schedule_segment_at(&cs->sched, tx);
    if (seg != cs->seg) {
        noise_params_t np = schedule_noise(&cs->sched, seg, o->model_set, o->model, o->p);
        char desc[200];
        noise_set_params(ch, &np);
        cs->seg = seg;
        noise_describe(&np, desc, sizeof desc);
        term_say(TAG, COL, "%s%s=== interference changes: %s from transmission %ld (%s) ===%s",
                 term_c(T_BOLD), term_c(T_YELLOW), cs->sched.seg[seg].profile->label, tx, desc,
                 term_c(T_RESET));
    }
    return cs->sched.seg[seg].profile;
}

/* The true tag ID: decode the CLEAN codeword (an error-free codeword decodes exactly). */
static void true_tag(const fec_config_t *fc, const uint8_t *clean, size_t nbits,
                     uint8_t id[TAG_ID_BYTES]) {
    uint8_t work[PACKET_MAX_BITS], payload[PAYLOAD_BITS];
    fec_result_t res;
    memcpy(work, clean, nbits);
    fec_decode(fc, work, nbits, payload, PAYLOAD_BITS, &res);
    frame_parse_payload(payload, id);
}

static void print_positions(const packet_t *p, const uint8_t *mask) {
    char list[160];
    size_t len = 0;
    int shown = 0;
    list[0] = '\0';
    for (int i = 0; i < p->nbits && len < sizeof list - 16; i++) {
        if (!mask[i]) continue;
        if (shown == 12) { len += (size_t)snprintf(list + len, sizeof list - len, " ..."); break; }
        len += (size_t)snprintf(list + len, sizeof list - len, "%s%d", shown ? "," : "", i);
        shown++;
    }
    const char *c = p->truth.flips ? term_c(T_RED) : term_c(T_GREEN);
    term_say(TAG, COL, "seq=%-4u %sflipped %2u/%u bits%s%s%s", p->seq, c, p->truth.flips,
             p->nbits, term_c(T_RESET), p->truth.flips ? " at " : "", list);
}

static int process_data(const cli_opts_t *o, const profile_t *prof, noise_t *ch, packet_t *p,
                        uint8_t *mask, FILE *log, channel_stats_t *st) {
    fec_config_t fc = fec_config((code_id_t)p->code);
    fc.rs_t = p->rs_t;
    fc.interleave = p->interleave;
    if (!fec_config_valid(&fc) || fec_coded_bits(&fc, PAYLOAD_BITS) != p->nbits) {
        term_say(TAG, COL, "dropping malformed frame seq=%u", p->seq);
        return -1;
    }
    memcpy(p->truth.clean_bits, p->bits, p->nbits);
    true_tag(&fc, p->truth.clean_bits, p->nbits, p->truth.tag);
    size_t flips = noise_apply(ch, p->bits, p->nbits, mask);  /* <-- the interference */
    p->has_truth = 1;
    p->truth.flips = (uint16_t)flips;
    p->truth.profile = (uint8_t)profile_id(prof);
    p->truth.model = (uint8_t)ch->params.model;
    st->frames++;
    st->bits += p->nbits;
    st->flips += flips;
    if (!o->quiet) print_positions(p, mask);
    if (log) {
        fprintf(log, "%u,%s,%u,%zu,", p->seq, fec_code_name(fc.code), p->nbits, flips);
        for (int i = 0, first = 1; i < p->nbits; i++)
            if (mask[i]) { fprintf(log, "%s%d", first ? "" : " ", i); first = 0; }
        fputc('\n', log);
    }
    return 0;
}

static int forward(int out, const cli_opts_t *o, const packet_t *p) {
    uint8_t wire[PACKET_MAX_BYTES];
    size_t len = packet_serialize(p, wire, sizeof wire);
    return len ? net_sendto(out, o->host, o->tollgate_port, wire, len) : -1;
}

int main(int argc, char **argv) {
    static packet_t pkt;
    static uint8_t mask[PACKET_MAX_BITS], wire[PACKET_MAX_BYTES];
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    term_init(o.color);
    install_stop_handler();

    channel_sched_t cs;
    memset(&cs, 0, sizeof cs);
    if (o.schedule) {
        if (schedule_parse(o.schedule, &cs.sched) != 0) {
            fprintf(stderr, "error: bad --schedule '%s' (example: toll:200,hospital:200)\n", o.schedule);
            return 2;
        }
        cs.active = 1;
        o.profile = cs.sched.seg[0].profile;
    }
    noise_params_t np = cli_noise_params(&o);
    noise_t ch;
    noise_init(&ch, &np, rng_derive(o.seed, "channel"));
    char desc[200];
    noise_describe(&np, desc, sizeof desc);

    int in = net_udp_bind(o.host, o.channel_port);
    int out = net_udp_socket();
    if (in < 0 || out < 0) return 1;
    FILE *log = o.log_path ? fopen(o.log_path, "w") : NULL;
    if (log) fprintf(log, "seq,code,nbits,flips,positions\n");

    term_say(TAG, COL, "listening on %s:%d -> tollgate :%d | profile %s | %s",
             o.host, o.channel_port, o.tollgate_port, o.profile->label, desc);
    if (cs.active) term_say(TAG, COL, "schedule: %s (counted in transmissions)", o.schedule);

    channel_stats_t st = {0, 0, 0};
    int got_first = 0;
    while (!stop_requested()) {
        long n = net_recv(in, wire, sizeof wire, got_first && o.idle_timeout_ms ? o.idle_timeout_ms : -1);
        if (n == 0) { term_say(TAG, COL, "idle timeout - stopping"); break; }
        if (n < 0) continue; /* interrupted: loop re-checks the stop flag */
        if (packet_parse(wire, (size_t)n, &pkt) != 0) {
            term_say(TAG, COL, "ignoring malformed packet (%ld bytes)", n);
            continue;
        }
        got_first = 1;
        if (pkt.type == PKT_END) break;
        const profile_t *prof = apply_schedule(&o, &cs, &ch, st.frames);
        if (process_data(&o, prof, &ch, &pkt, mask, log, &st) == 0) forward(out, &o, &pkt);
    }
    double ber = st.bits ? (double)st.flips / (double)st.bits : 0.0;
    term_say(TAG, COL, "done: %ld frames, %llu bits flipped of %llu (measured BER %.5f, model BER %.5f)",
             st.frames, st.flips, st.bits, ber, noise_expected_ber(&np));
    { /* tell the tollgate we are done; sent twice in case one copy is lost */
        packet_t end;
        memset(&end, 0, sizeof end);
        end.type = PKT_END;
        end.seq = pkt.type == PKT_END ? pkt.seq : (uint32_t)st.frames;
        forward(out, &o, &end);
        sleep_us(20000);
        forward(out, &o, &end);
    }
    if (log) fclose(log);
    net_close(in);
    net_close(out);
    return 0;
}
