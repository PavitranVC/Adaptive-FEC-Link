#include <stdlib.h>
#include <string.h>
#include "frame.h"
#include "linksim.h"
#include "profiles.h"
#include "receiver.h"
#include "window.h"

void linksim_defaults(linksim_params_t *p) {
    memset(p, 0, sizeof *p);
    p->strategy = STRAT_FEC;
    p->fixed = fec_config(CODE_HAMMING74);
    adapt_defaults(&p->adapt);
    p->max_retries = 4;
    p->rtt_ms = 20.0;
    p->timeout_ms = 100.0;
    p->bitrate_kbps = 160.0;
    p->frames = 1000;
    p->seed = 1;
    p->nseg = 1;
    p->seg[0].noise = profile_noise(&PROFILE_TOLL_PLAZA, PROFILE_TOLL_PLAZA.default_model, -1.0);
    p->seg[0].label = PROFILE_TOLL_PLAZA.label;
}

/* Which segment does transmission number tx fall into? */
static int segment_of(const linksim_params_t *p, long tx) {
    long end = 0;
    for (int i = 0; i < p->nseg - 1; i++) {
        end += p->seg[i].frames;
        if (tx < end) return i;
    }
    return p->nseg - 1;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef struct {               /* running state of one simulation */
    const linksim_params_t *p;
    noise_t ch;
    int seg;
    long tx;
    rng_t fb_rng;
    window_t win;
    sender_t snd;
    double rate_sum, level_sum;
} sim_state_t;

/* Send the current attempt; returns the receiver's decode result and fills *t_rx. */
static void transmit(sim_state_t *s, const uint8_t *payload, double t_start, rx_decode_t *d,
                     double *t_rx, int *nbits) {
    int seg = segment_of(s->p, s->tx);
    if (seg != s->seg) {                         /* scheduled change of interference */
        s->seg = seg;
        noise_set_params(&s->ch, &s->p->seg[seg].noise);
    }
    fec_config_t cfg = sender_config(&s->snd);
    uint8_t coded[FEC_MAX_CODED_BITS];
    *nbits = (int)fec_encode(&cfg, payload, PAYLOAD_BITS, coded);
    noise_apply(&s->ch, coded, (size_t)*nbits, NULL);
    s->tx++;
    s->rate_sum += (double)PAYLOAD_BITS / *nbits;
    s->level_sum += ladder_level_of(&cfg);
    rx_decode(&cfg, coded, (size_t)*nbits, d);
    double airtime = *nbits / s->p->bitrate_kbps;
    *t_rx = t_start + airtime + s->p->rtt_ms / 2 + d->decode_us / 1000.0;
}

void linksim_run(const linksim_params_t *p, linksim_result_t *r, FILE *trace) {
    static sim_state_t s;
    memset(&s, 0, sizeof s);
    s.p = p;
    noise_init(&s.ch, &p->seg[0].noise, rng_derive(p->seed, "linksim-noise"));
    rng_seed(&s.fb_rng, rng_derive(p->seed, "linksim-fbdrop"));
    window_init(&s.win, p->adapt.window);
    sender_init(&s.snd, p->strategy, &p->fixed, &p->adapt, p->max_retries);
    rng_t tags;
    rng_seed(&tags, rng_derive(p->seed, "linksim-tags"));
    double *lat = malloc(sizeof(double) * (size_t)(p->frames > 0 ? p->frames : 1));
    long nlat = 0;
    memset(r, 0, sizeof *r);
    if (trace) fprintf(trace, "frame,tx,segment,level,attempts,class,latency_ms\n");

    for (long f = 0; f < p->frames; f++) {
        uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
        tag_random(&tags, id);
        frame_build_payload(id, payload);
        sender_begin_frame(&s.snd, (uint32_t)f);
        double t = 0.0, delivered_at = -1.0;
        frame_class_t cls = CLASS_DETECTED_FAIL;
        int attempts = 0, level = s.snd.strategy == STRAT_ADAPTIVE ? s.snd.ctl.level : -1;
        long first_tx = s.tx;
        if (p->level_trace) p->level_trace[f] = level;
        if (p->segment_trace) p->segment_trace[f] = segment_of(p, first_tx);

        for (;;) {
            rx_decode_t d;
            double t_rx;
            int nbits;
            transmit(&s, payload, t, &d, &t_rx, &nbits);
            attempts++;
            int ok = rx_accept(&d);
            if (ok && delivered_at < 0) {                 /* first valid copy = delivery */
                delivered_at = t_rx;
                cls = frame_classify(d.crc_ok, d.failed_blocks > 0, d.id, id);
            }
            if (!sender_needs_feedback(&s.snd)) break;   /* plain FEC: one shot */

            /* receiver: update the window, answer ACK (have a valid copy) or NAK */
            feedback_t fb;
            memset(&fb, 0, sizeof fb);
            int lvl = ladder_level_of(&d.cfg);
            window_push(&s.win, lvl, !ok, d.corrected_bits, nbits);
            window_fill_feedback(&s.win, &fb);
            fb.type = (ok || delivered_at >= 0) ? FB_ACK : FB_NAK;
            fb.seq = (uint32_t)f;
            fb.attempt = (uint8_t)s.snd.attempt;
            fb.level = (uint8_t)(lvl < 0 ? 255 : lvl);

            send_action_t act;
            if (!rng_bernoulli(&s.fb_rng, p->fb_drop)) {
                act = sender_on_feedback(&s.snd, &fb, NULL);
                t = t_rx + p->rtt_ms / 2;
            } else {
                act = sender_on_timeout(&s.snd);       /* feedback lost on the way back */
                t = t + p->timeout_ms;
            }
            if (act != SEND_RETRY) break;               /* DONE or GIVE_UP */
        }

        if (delivered_at >= 0) lat[nlat++] = delivered_at;
        if (cls == CLASS_CORRECT) r->correct++;
        else if (cls == CLASS_SILENT_WRONG) r->silent_wrong++;
        else r->failed++;
        if (trace)
            fprintf(trace, "%ld,%ld,%s,%d,%d,%s,%.3f\n", f, first_tx,
                    p->seg[segment_of(p, first_tx)].label ? p->seg[segment_of(p, first_tx)].label : "",
                    level, attempts, frame_class_name(cls), delivered_at);
    }

    r->frames = p->frames;
    r->transmissions = s.tx;
    r->timeouts = s.snd.timeouts;
    r->level_changes = s.snd.ctl.changes;
    double n = p->frames > 0 ? (double)p->frames : 1.0;
    r->success_rate = r->correct / n;
    r->silent_wrong_rate = r->silent_wrong / n;
    r->retx_per_frame = (double)(s.tx - p->frames) / n;
    r->mean_code_rate = s.tx ? s.rate_sum / s.tx : 0.0;
    r->mean_level = s.tx ? s.level_sum / s.tx : 0.0;
    if (nlat > 0) {
        double sum = 0;
        for (long i = 0; i < nlat; i++) sum += lat[i];
        qsort(lat, (size_t)nlat, sizeof lat[0], cmp_double);
        r->mean_latency_ms = sum / nlat;
        long idx = (long)(0.99 * (nlat - 1) + 0.5);       /* nearest-rank 99th percentile */
        r->p99_latency_ms = lat[idx];
    }
    free(lat);
}
