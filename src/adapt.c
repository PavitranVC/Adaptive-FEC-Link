#include <math.h>
#include <stdio.h>
#include <string.h>
#include "adapt.h"

/* ------------------------------------------------------------------------------------------
 * Ladder - edit this table to change the levels.
 * ------------------------------------------------------------------------------------------ */
typedef struct { code_id_t code; int rs_t; int interleave; const char *label; } rung_t;

static const rung_t LADDER[] = {
    {CODE_NONE, 4, 1, "L0 uncoded"},
    {CODE_HAMMING74, 4, 1, "L1 Hamming(7,4)"},
    {CODE_BCH157, 4, 1, "L2 BCH(15,7)"},
    {CODE_BCH3116, 4, 1, "L3 BCH(31,16)"},
    {CODE_RS, 4, 1, "L4 RS(24,16) t=4"},
    {CODE_RS, 8, 1, "L5 RS(32,16) t=8"},
};
#define NLEVELS ((int)(sizeof LADDER / sizeof LADDER[0]))

int ladder_levels(void) { return NLEVELS; }

static int clamp_level(int l) { return l < 0 ? 0 : l >= NLEVELS ? NLEVELS - 1 : l; }

fec_config_t ladder_config(int level) {
    const rung_t *r = &LADDER[clamp_level(level)];
    fec_config_t c = fec_config(r->code);
    c.rs_t = r->rs_t;
    c.interleave = r->interleave;
    return c;
}

const char *ladder_label(int level) { return LADDER[clamp_level(level)].label; }

int ladder_level_of(const fec_config_t *cfg) {
    for (int l = 0; l < NLEVELS; l++) {
        const rung_t *r = &LADDER[l];
        if (r->code == cfg->code && r->interleave == cfg->interleave &&
            (r->code != CODE_RS || r->rs_t == cfg->rs_t))
            return l;
    }
    return -1;
}

/* ------------------------------------------------------------------------------------------
 * Failure prediction (binomial model)
 * ------------------------------------------------------------------------------------------ */

/* P(at most t of n independent symbols are wrong), each wrong with probability p. */
static double binom_cdf(int n, int t, double p) {
    double sum = 0.0;
    for (int i = 0; i <= t && i <= n; i++) {
        double c = 1.0;                                    /* C(n, i) */
        for (int j = 1; j <= i; j++) c = c * (n - i + j) / j;
        sum += c * pow(p, i) * pow(1.0 - p, n - i);
    }
    return sum > 1.0 ? 1.0 : sum;
}

double adapt_predict_fail(const fec_config_t *cfg, double ber) {
    int n, t, blocks;
    double p = ber;
    if (cfg->code == CODE_RS) {
        n = 16 + 2 * cfg->rs_t;          /* bytes */
        t = cfg->rs_t;
        blocks = 1;
        p = 1.0 - pow(1.0 - ber, 8);     /* a byte is wrong if any of its 8 bits is */
    } else {
        const fec_code_info_t *info = fec_code_info(cfg->code);
        n = info->n;
        t = info->t;
        blocks = (int)(fec_coded_bits(cfg, PAYLOAD_BITS_DEFAULT) / (size_t)n);
    }
    return 1.0 - pow(binom_cdf(n, t, p), blocks);
}

/* ------------------------------------------------------------------------------------------
 * Controller
 * ------------------------------------------------------------------------------------------ */

void adapt_defaults(adapt_params_t *p) {
    p->window = 32;
    p->up_threshold = 0.20;
    p->down_threshold = 0.05;
    p->start_level = 1;
    p->min_level = 0;
    p->max_level = NLEVELS - 1;
    p->max_hold = 16;
}

void adapt_init(adapt_t *a, const adapt_params_t *p) {
    memset(a, 0, sizeof *a);
    a->p = *p;
    if (a->p.max_level >= NLEVELS) a->p.max_level = NLEVELS - 1;
    if (a->p.min_level < 0) a->p.min_level = 0;
    a->level = a->p.start_level < a->p.min_level ? a->p.min_level
             : a->p.start_level > a->p.max_level ? a->p.max_level : a->p.start_level;
    a->hold = 1;
}

static int move(adapt_t *a, int dir) {
    a->level += dir;
    a->frames_at_level = 0;
    a->since_eval = 0;
    a->changes++;
    if (dir > 0) a->ups++; else a->downs++;
    return dir;
}

int adapt_update(adapt_t *a, const feedback_t *fb) {
    if (fb->level != a->level) return 0;              /* describes another level: stale */
    a->frames_at_level++;
    a->since_eval++;
    /* decide once per W fresh frames: consecutive decisions look at disjoint windows, so a
     * single unlucky stretch cannot trigger the same rule W times in a row */
    if (fb->win_n < a->p.window || a->since_eval < a->p.window) return 0;
    a->since_eval = 0;

    double f = (double)fb->win_fail / fb->win_n;
    double ber = fb->win_bits ? (double)fb->win_corrected / fb->win_bits : 0.0;
    a->last_fail = f;
    a->last_ber = ber;
    a->last_pred = -1.0;
    long W = a->p.window;

    /* UP: the current code is not coping */
    if (f > a->p.up_threshold && a->level < a->p.max_level) {
        if (a->probing && a->frames_at_level <= 2 * W) {       /* the down-probe failed */
            a->failed_probes++;
            a->hold = a->hold * 2 > a->p.max_hold ? a->p.max_hold : a->hold * 2;
        }
        a->probing = 0;
        return move(a, +1);
    }
    /* a probe that survived 2W frames was right: forget the back-off */
    if (a->probing && a->frames_at_level > 2 * W) { a->probing = 0; a->hold = 1; }

    /* DOWN: copes now, the cheaper code is predicted to cope, and we have waited long enough */
    if (a->level > a->p.min_level && f <= a->p.down_threshold &&
        a->frames_at_level >= (long)a->hold * W) {
        fec_config_t lower = ladder_config(a->level - 1);
        a->last_pred = adapt_predict_fail(&lower, ber);
        if (a->last_pred < a->p.down_threshold) {
            a->probing = 1;
            return move(a, -1);
        }
    }
    return 0;
}
