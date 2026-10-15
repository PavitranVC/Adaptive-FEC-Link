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
 *   HOSPITAL_IMAGING (profile default models) and, with --schedule, the scheduled channel
 *   (channel = "schedule"); --trace FILE writes the adaptive controller's per-frame level there.
 *   CSV: strategy,code,channel,frames,success_rate,silent_wrong_rate,retx_per_frame,
 *        mean_latency_ms,p99_latency_ms,mean_code_rate,level_changes,goodput
 *   goodput = CORRECT frames * 128 payload bits / all coded bits sent (parity + retransmissions)
 *
 * MULTI-SEED MODE (overnight task 3):  bin/bench --strategies --seeds 20 [--schedule ...]
 *   Runs the same comparison for seeds seed, seed+1, ..., seed+N-1 and writes, per row, the mean
 *   and the 95% confidence interval (Student t, N-1 degrees of freedom, see stats.h) of success
 *   rate, goodput, retransmissions/frame and p99 latency to --out (default
 *   results/bench_strategy_seeds.csv). Also writes <out>_raw.csv (one row per seed) and
 *   <out>_diff.csv: PAIRED differences adaptive - HARQ+RS t=8 and adaptive - HARQ+RS t=4 on the
 *   same seeds (mean, 95% CI, significant = CI excludes 0). Without --seeds nothing changes.
 */
#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "linksim.h"
#include "schedule.h"
#include "sim.h"
#include "stats.h"

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

typedef struct { const char *label; strategy_t strategy; code_id_t code; int rs_t; } strat_row_t;

/* Fixed baselines include the two strongest codes of the adaptive ladder (RS t=4, RS t=8) with
 * HARQ, so the adaptive controller is compared against "always use the strongest code". */
static const strat_row_t STRAT_ROWS[] = {
    {"fec", STRAT_FEC, CODE_HAMMING74, 4}, {"fec", STRAT_FEC, CODE_BCH157, 4},
    {"fec", STRAT_FEC, CODE_BCH3116, 4},   {"fec", STRAT_FEC, CODE_RS, 4},
    {"fec", STRAT_FEC, CODE_RS, 8},
    {"arq", STRAT_ARQ, CODE_NONE, 4},
    {"harq", STRAT_HARQ, CODE_HAMMING74, 4}, {"harq", STRAT_HARQ, CODE_BCH157, 4},
    {"harq", STRAT_HARQ, CODE_BCH3116, 4},   {"harq", STRAT_HARQ, CODE_RS, 4},
    {"harq", STRAT_HARQ, CODE_RS, 8},
    {"adaptive", STRAT_ADAPTIVE, CODE_NONE, 4},
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

static void row_code_name(const strat_row_t *row, char *code, size_t cap) {
    if (row->strategy == STRAT_ADAPTIVE) snprintf(code, cap, "ladder");
    else if (row->code == CODE_RS) snprintf(code, cap, "rs_t%d", row->rs_t);
    else snprintf(code, cap, "%s", fec_code_name(row->code));
}

static void run_row(linksim_params_t *lp, const strat_row_t *row, linksim_result_t *r, FILE *trace) {
    lp->strategy = row->strategy;
    lp->fixed = fec_config(row->code);
    lp->fixed.rs_t = row->rs_t;
    linksim_run(lp, r, trace);
}

static void strategy_row(FILE *csv, const char *channel, linksim_params_t *lp,
                         const strat_row_t *row, FILE *trace) {
    linksim_result_t r;
    run_row(lp, row, &r, trace);
    char code[24];
    row_code_name(row, code, sizeof code);
    fprintf(csv, "%s,%s,%s,%ld,%.6f,%.6f,%.4f,%.3f,%.3f,%.4f,%ld,%.4f\n", row->label, code, channel,
            r.frames, r.success_rate, r.silent_wrong_rate, r.retx_per_frame, r.mean_latency_ms,
            r.p99_latency_ms, r.mean_code_rate, r.level_changes, r.goodput);
    printf("  %-9s %-10s %-17s success=%6.4f retx/frame=%6.3f p99=%7.2f ms rate=%.3f goodput=%.3f\n",
           row->label, code, channel, r.success_rate, r.retx_per_frame, r.p99_latency_ms,
           r.mean_code_rate, r.goodput);
}

static int run_strategies(const cli_opts_t *o) {
    const char *path = o->out_path ? o->out_path : "results/bench_strategy.csv";
    FILE *csv = fopen(path, "w");
    if (!csv) { perror(path); return 1; }
    fprintf(csv, "strategy,code,channel,frames,success_rate,silent_wrong_rate,retx_per_frame,"
                 "mean_latency_ms,p99_latency_ms,mean_code_rate,level_changes,goodput\n");
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
    if (o->schedule) {                       /* the scheduled channel: toll -> hospital -> toll */
        schedule_t sched;
        if (schedule_parse(o->schedule, &sched) != 0) {
            fprintf(stderr, "error: bad --schedule '%s'\n", o->schedule);
            fclose(csv);
            return 2;
        }
        linksim_params_t lp;
        base_params(o, &lp);
        schedule_to_linksim(&sched, o->model_set, o->model, o->p, &lp);
        for (size_t i = 0; i < sizeof STRAT_ROWS / sizeof STRAT_ROWS[0]; i++) {
            FILE *trace = NULL;
            if (STRAT_ROWS[i].strategy == STRAT_ADAPTIVE && o->trace_path)
                trace = fopen(o->trace_path, "w");
            strategy_row(csv, "schedule", &lp, &STRAT_ROWS[i], trace);
            if (trace) { fclose(trace); printf("bench: wrote %s\n", o->trace_path); }
        }
    }
    fclose(csv);
    printf("bench: wrote %s\n", path);
    return 0;
}

/* ------------------------------------------------------------ multi-seed comparison ---- */

#define N_ROWS ((int)(sizeof STRAT_ROWS / sizeof STRAT_ROWS[0]))
#define MAX_CH 3
enum { M_SUCCESS, M_GOODPUT, M_RETX, M_P99, N_METRICS };
static const char *const METRIC[N_METRICS] = {"success_rate", "goodput", "retx_per_frame",
                                              "p99_latency_ms"};

static double metric_of(const linksim_result_t *r, int m) {
    switch (m) {
    case M_SUCCESS: return r->success_rate;
    case M_GOODPUT: return r->goodput;
    case M_RETX:    return r->retx_per_frame;
    default:        return r->p99_latency_ms;
    }
}

static int find_row(strategy_t s, code_id_t code, int rs_t) {
    for (int i = 0; i < N_ROWS; i++)
        if (STRAT_ROWS[i].strategy == s && STRAT_ROWS[i].code == code &&
            (code != CODE_RS || STRAT_ROWS[i].rs_t == rs_t))
            return i;
    return -1;
}

/* "results/x.csv" + "_raw" -> "results/x_raw.csv" */
static void sibling_path(const char *path, const char *suffix, char *out, size_t cap) {
    const char *dot = strrchr(path, '.');
    size_t stem = dot ? (size_t)(dot - path) : strlen(path);
    snprintf(out, cap, "%.*s%s%s", (int)stem, path, suffix, dot ? dot : "");
}

static FILE *open_csv(const char *path, const char *header) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return NULL; }
    fprintf(f, "%s\n", header);
    return f;
}

/* Paired difference a - b over the same seeds, for every metric. */
static void write_diffs(FILE *f, const char *channel, linksim_result_t *ra, linksim_result_t *rb,
                        const char *a, const char *b, int n) {
    double d[1000];
    for (int m = 0; m < N_METRICS; m++) {
        for (int i = 0; i < n; i++) d[i] = metric_of(&ra[i], m) - metric_of(&rb[i], m);
        stats_ci_t c = stats_mean_ci(d, n);
        int sig = c.lo > 0 || c.hi < 0;
        fprintf(f, "%s,%s,%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%d\n", channel, a, b, METRIC[m], n,
                c.mean, c.half, c.lo, c.hi, sig);
        printf("  paired %-17s %-14s - %-14s %-15s %+9.4f +- %.4f %s\n", channel, a, b, METRIC[m],
               c.mean, c.half, sig ? "(CI excludes 0)" : "(CI includes 0: no clear difference)");
    }
}

static int run_strategies_seeds(const cli_opts_t *o) {
    const char *path = o->out_path ? o->out_path : "results/bench_strategy_seeds.csv";
    char raw_path[512], diff_path[512];
    sibling_path(path, "_raw", raw_path, sizeof raw_path);
    sibling_path(path, "_diff", diff_path, sizeof diff_path);
    int n = o->seeds;
    linksim_params_t lp[MAX_CH];
    const char *chname[MAX_CH];
    int nch = 0;
    const profile_t *profs[2] = {&PROFILE_TOLL_PLAZA, &PROFILE_HOSPITAL_IMAGING};
    for (int c = 0; c < 2; c++, nch++) {
        base_params(o, &lp[nch]);
        profile_channel(profs[c], &lp[nch]);
        chname[nch] = profs[c]->label;
    }
    if (o->schedule) {
        schedule_t sched;
        if (schedule_parse(o->schedule, &sched) != 0) {
            fprintf(stderr, "error: bad --schedule '%s'\n", o->schedule);
            return 2;
        }
        base_params(o, &lp[nch]);
        schedule_to_linksim(&sched, o->model_set, o->model, o->p, &lp[nch]);
        chname[nch++] = "schedule";
    }
    static linksim_result_t res[MAX_CH][N_ROWS][1000];
    printf("bench --strategies --seeds %d: seeds %llu..%llu, %ld frames per row and seed -> %s\n",
           n, (unsigned long long)o->seed, (unsigned long long)(o->seed + (uint64_t)n - 1), o->frames, path);
    FILE *raw = open_csv(raw_path, "strategy,code,channel,seed,frames,success_rate,silent_wrong_rate,"
                         "retx_per_frame,mean_latency_ms,p99_latency_ms,mean_code_rate,level_changes,goodput");
    if (!raw) return 1;
    for (int c = 0; c < nch; c++)
        for (int i = 0; i < N_ROWS; i++)
            for (int k = 0; k < n; k++) {
                linksim_result_t *r = &res[c][i][k];
                char code[24];
                lp[c].seed = o->seed + (uint64_t)k;
                run_row(&lp[c], &STRAT_ROWS[i], r, NULL);
                row_code_name(&STRAT_ROWS[i], code, sizeof code);
                fprintf(raw, "%s,%s,%s,%llu,%ld,%.6f,%.6f,%.4f,%.3f,%.3f,%.4f,%ld,%.4f\n",
                        STRAT_ROWS[i].label, code, chname[c], (unsigned long long)lp[c].seed,
                        r->frames, r->success_rate, r->silent_wrong_rate, r->retx_per_frame,
                        r->mean_latency_ms, r->p99_latency_ms, r->mean_code_rate,
                        r->level_changes, r->goodput);
            }
    fclose(raw);

    FILE *csv = open_csv(path, "strategy,code,channel,seeds,frames,success_mean,success_ci95,"
                         "goodput_mean,goodput_ci95,retx_mean,retx_ci95,p99_mean,p99_ci95");
    if (!csv) return 1;
    for (int c = 0; c < nch; c++)
        for (int i = 0; i < N_ROWS; i++) {
            stats_ci_t st[N_METRICS];
            double x[1000];
            char code[24];
            for (int m = 0; m < N_METRICS; m++) {
                for (int k = 0; k < n; k++) x[k] = metric_of(&res[c][i][k], m);
                st[m] = stats_mean_ci(x, n);
            }
            row_code_name(&STRAT_ROWS[i], code, sizeof code);
            fprintf(csv, "%s,%s,%s,%d,%ld,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f\n",
                    STRAT_ROWS[i].label, code, chname[c], n, o->frames, st[0].mean, st[0].half,
                    st[1].mean, st[1].half, st[2].mean, st[2].half, st[3].mean, st[3].half);
            printf("  %-9s %-10s %-17s success=%.4f+-%.4f goodput=%.4f+-%.4f retx=%.3f+-%.3f p99=%.2f+-%.2f ms\n",
                   STRAT_ROWS[i].label, code, chname[c], st[0].mean, st[0].half, st[1].mean,
                   st[1].half, st[2].mean, st[2].half, st[3].mean, st[3].half);
        }
    fclose(csv);

    FILE *diff = open_csv(diff_path, "channel,a,b,metric,seeds,mean_diff,ci95,lo,hi,significant");
    if (!diff) return 1;
    int ad = find_row(STRAT_ADAPTIVE, CODE_NONE, 4), h8 = find_row(STRAT_HARQ, CODE_RS, 8),
        h4 = find_row(STRAT_HARQ, CODE_RS, 4);
    for (int c = 0; c < nch; c++) {
        write_diffs(diff, chname[c], res[c][ad], res[c][h8], "adaptive", "harq_rs_t8", n);
        write_diffs(diff, chname[c], res[c][ad], res[c][h4], "adaptive", "harq_rs_t4", n);
    }
    fclose(diff);
    printf("bench: wrote %s, %s, %s (95%% CI: Student t, %d degrees of freedom, t = %.3f)\n",
           path, raw_path, diff_path, n - 1, stats_t975(n - 1));
    return 0;
}

int main(int argc, char **argv) {
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    if (o.strategies) return o.seeds > 0 ? run_strategies_seeds(&o) : run_strategies(&o);
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
