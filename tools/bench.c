/*
 * bench - in-process benchmark (no sockets).
 *
 * Sweeps  code x noise model x parameter  and simulates --frames frames per point with the same
 * encode -> noise -> decode -> CRC -> classify chain as the UDP pipeline. Writes a CSV:
 *   code,model,param,frames,frame_success_rate,detected_fail_rate,silent_wrong_rate,
 *   code_rate,mean_decode_us
 *
 * The swept parameter ("param") per model:
 *   bsc   : flip probability p
 *   ge    : p_gb (probability per bit that a burst starts); p_bg, e_good, e_bad from --profile
 *   burst : burst length L in bits, one burst in EVERY frame (density from --profile)
 * Besides every registered code (RS with --rs-t, default 4) it also benchmarks interleaved
 * variants "hamming74+il<D>" and "bch3116+il<D>" (D = --interleave if > 1, else 8).
 * Optional filters: --code X, --model Y. Usage: bin/bench --profile toll --frames 5000 --out f.csv
 *
 * STRATEGY MODE (week 2):  bin/bench --strategies --frames N --out results/bench_strategy.csv
 *   Compares fec (fixed codes) vs arq vs harq vs adaptive with the link simulator (linksim.h,
 *   simulated clock: RTT, timeout, airtime at 160 kbit/s). Channels: TOLL_PLAZA and
 *   HOSPITAL_IMAGING (profile default models) and, with --schedule, the scheduled channel.
 *   CSV: strategy,code,channel,frames,success_rate,silent_wrong_rate,retx_per_frame,
 *        mean_latency_ms,p99_latency_ms,mean_code_rate,level_changes
 */
#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "linksim.h"
#include "sim.h"

static const double BSC_P[] = {0.0005, 0.001, 0.002, 0.005, 0.01, 0.015, 0.02, 0.03, 0.05, 0.07, 0.1};
static const double GE_PGB[] = {0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05};
static const double BURST_L[] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static noise_params_t point_params(const profile_t *prof, noise_model_t m, double param) {
    noise_params_t np = profile_noise(prof, m, m == NOISE_BURST ? 1.0 : param);
    if (m == NOISE_BURST) np.burst.length = (int)param;
    return np;
}

typedef struct {
    char name[32];
    fec_config_t cfg;
} variant_t;

static void run_point(FILE *csv, const cli_opts_t *o, const variant_t *v, noise_model_t m,
                      double param) {
    noise_params_t np = point_params(o->profile, m, param);
    sim_result_t r;
    sim_run(&v->cfg, &np, o->frames, o->seed, &r);
    double n = (double)r.frames;
    fprintf(csv, "%s,%s,%g,%ld,%.6f,%.6f,%.6f,%.4f,%.3f\n", v->name,
            noise_model_name(m), param, r.frames, r.correct / n, r.detected_fail / n,
            r.silent_wrong / n, r.code_rate, r.mean_decode_us);
    printf("  %-14s %-5s param=%-7g success=%6.4f detected=%6.4f silent=%6.4f rate=%.3f %6.2fus\n",
           v->name, noise_model_name(m), param, r.correct / n, r.detected_fail / n,
           r.silent_wrong / n, r.code_rate, r.mean_decode_us);
}

static int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], flag, strlen(flag))) return 1;
    return 0;
}

static int build_variants(const cli_opts_t *o, int only_code, variant_t *vs) {
    int nv = 0, depth = o->interleave > 1 ? o->interleave : 8;
    for (int ci = 0; ci < fec_code_count(); ci++) {
        code_id_t code = fec_code_at(ci)->id;
        if (only_code && code != o->code) continue;
        vs[nv].cfg = fec_config(code);
        vs[nv].cfg.rs_t = o->rs_t;
        if (code == CODE_RS && o->rs_t != 4)
            snprintf(vs[nv].name, sizeof vs[nv].name, "rs_t%d", o->rs_t);
        else
            snprintf(vs[nv].name, sizeof vs[nv].name, "%s", fec_code_name(code));
        nv++;
        if (code == CODE_HAMMING74 || code == CODE_BCH3116) {   /* interleaved variant */
            vs[nv] = vs[nv - 1];
            vs[nv].cfg.interleave = depth;
            snprintf(vs[nv].name, sizeof vs[nv].name, "%s+il%d", fec_code_name(code), depth);
            nv++;
        }
    }
    return nv;
}

/* ---------------------------------------------------------------- strategy comparison ---- */

typedef struct { const char *label; strategy_t strategy; code_id_t code; } strat_row_t;

static const strat_row_t STRAT_ROWS[] = {
    {"fec", STRAT_FEC, CODE_HAMMING74}, {"fec", STRAT_FEC, CODE_BCH157},
    {"fec", STRAT_FEC, CODE_BCH3116},   {"fec", STRAT_FEC, CODE_RS},
    {"arq", STRAT_ARQ, CODE_NONE},
    {"harq", STRAT_HARQ, CODE_HAMMING74}, {"harq", STRAT_HARQ, CODE_BCH157},
    {"harq", STRAT_HARQ, CODE_BCH3116},
};

static void base_params(const cli_opts_t *o, linksim_params_t *lp) {
    linksim_defaults(lp);
    lp->adapt = cli_adapt_params(o);
    lp->max_retries = o->max_retries;
    lp->rtt_ms = o->rtt_ms;
    lp->timeout_ms = o->timeout_ms;
    lp->fb_drop = o->fb_drop;
    lp->frames = o->frames;
    lp->seed = o->seed;
}

static void profile_channel(const profile_t *prof, linksim_params_t *lp) {
    lp->nseg = 1;
    lp->seg[0].noise = profile_noise(prof, prof->default_model, -1.0);
    lp->seg[0].frames = 0;
    lp->seg[0].label = prof->label;
}

static void strategy_row(FILE *csv, const char *channel, linksim_params_t *lp,
                         const strat_row_t *row, FILE *trace) {
    lp->strategy = row->strategy;
    lp->fixed = fec_config(row->code);
    linksim_result_t r;
    linksim_run(lp, &r, trace);
    const char *code = row->strategy == STRAT_ADAPTIVE ? "ladder" : fec_code_name(row->code);
    fprintf(csv, "%s,%s,%s,%ld,%.6f,%.6f,%.4f,%.3f,%.3f,%.4f,%ld\n", row->label, code, channel,
            r.frames, r.success_rate, r.silent_wrong_rate, r.retx_per_frame, r.mean_latency_ms,
            r.p99_latency_ms, r.mean_code_rate, r.level_changes);
    printf("  %-9s %-10s %-17s success=%6.4f retx/frame=%6.3f latency mean=%7.2f ms p99=%7.2f ms rate=%.3f\n",
           row->label, code, channel, r.success_rate, r.retx_per_frame, r.mean_latency_ms,
           r.p99_latency_ms, r.mean_code_rate);
}

static int run_strategies(const cli_opts_t *o) {
    const char *path = o->out_path ? o->out_path : "results/bench_strategy.csv";
    FILE *csv = fopen(path, "w");
    if (!csv) { perror(path); return 1; }
    fprintf(csv, "strategy,code,channel,frames,success_rate,silent_wrong_rate,retx_per_frame,"
                 "mean_latency_ms,p99_latency_ms,mean_code_rate,level_changes\n");
    printf("bench --strategies: %ld frames per row, RTT %d ms, timeout %d ms, max %d retries -> %s\n",
           o->frames, o->rtt_ms, o->timeout_ms, o->max_retries, path);
    const profile_t *profs[2] = {&PROFILE_TOLL_PLAZA, &PROFILE_HOSPITAL_IMAGING};
    for (int c = 0; c < 2; c++) {
        linksim_params_t lp;
        base_params(o, &lp);
        profile_channel(profs[c], &lp);
        for (size_t i = 0; i < sizeof STRAT_ROWS / sizeof STRAT_ROWS[0]; i++)
            strategy_row(csv, profs[c]->label, &lp, &STRAT_ROWS[i], NULL);
    }
    fclose(csv);
    printf("bench: wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    if (o.strategies) return run_strategies(&o);
    int only_code = has_flag(argc, argv, "--code"), only_model = o.model_set;
    const char *path = o.out_path ? o.out_path : "results/bench.csv";
    FILE *csv = fopen(path, "w");
    if (!csv) { perror(path); return 1; }
    fprintf(csv, "code,model,param,frames,frame_success_rate,detected_fail_rate,"
                 "silent_wrong_rate,code_rate,mean_decode_us\n");
    printf("bench: profile %s, %ld frames per point, seed %llu -> %s\n", o.profile->label,
           o.frames, (unsigned long long)o.seed, path);

    variant_t vs[16];
    int nv = build_variants(&o, only_code, vs);
    for (int vi = 0; vi < nv; vi++) {
        for (int m = NOISE_BSC; m <= NOISE_BURST; m++) {
            if (only_model && (noise_model_t)m != o.model) continue;
            const double *ps = m == NOISE_BSC ? BSC_P : m == NOISE_GE ? GE_PGB : BURST_L;
            int np = m == NOISE_BSC ? COUNT(BSC_P) : m == NOISE_GE ? COUNT(GE_PGB) : COUNT(BURST_L);
            for (int i = 0; i < np; i++) run_point(csv, &o, &vs[vi], (noise_model_t)m, ps[i]);
        }
    }
    fclose(csv);
    printf("bench: wrote %s\n", path);
    return 0;
}
