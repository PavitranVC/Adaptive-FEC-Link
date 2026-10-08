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
 * Optional filters: --code X, --model Y. Usage: bin/bench --profile toll --frames 5000 --out f.csv
 */
#include <stdio.h>
#include <string.h>
#include "cli.h"
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

static void run_point(FILE *csv, const cli_opts_t *o, code_id_t code, noise_model_t m,
                      double param) {
    fec_config_t fc = cli_fec_config(o);
    fc.code = code;
    noise_params_t np = point_params(o->profile, m, param);
    sim_result_t r;
    sim_run(&fc, &np, o->frames, o->seed, &r);
    double n = (double)r.frames;
    fprintf(csv, "%s,%s,%g,%ld,%.6f,%.6f,%.6f,%.4f,%.3f\n", fec_code_name(code),
            noise_model_name(m), param, r.frames, r.correct / n, r.detected_fail / n,
            r.silent_wrong / n, r.code_rate, r.mean_decode_us);
    printf("  %-11s %-5s param=%-7g success=%6.4f detected=%6.4f silent=%6.4f rate=%.3f %6.2fus\n",
           fec_code_name(code), noise_model_name(m), param, r.correct / n, r.detected_fail / n,
           r.silent_wrong / n, r.code_rate, r.mean_decode_us);
}

static int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; i++)
        if (!strncmp(argv[i], flag, strlen(flag))) return 1;
    return 0;
}

int main(int argc, char **argv) {
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    int only_code = has_flag(argc, argv, "--code"), only_model = o.model_set;
    const char *path = o.out_path ? o.out_path : "results/bench.csv";
    FILE *csv = fopen(path, "w");
    if (!csv) { perror(path); return 1; }
    fprintf(csv, "code,model,param,frames,frame_success_rate,detected_fail_rate,"
                 "silent_wrong_rate,code_rate,mean_decode_us\n");
    printf("bench: profile %s, %ld frames per point, seed %llu -> %s\n", o.profile->label,
           o.frames, (unsigned long long)o.seed, path);

    for (int ci = 0; ci < fec_code_count(); ci++) {
        code_id_t code = fec_code_at(ci)->id;
        if (only_code && code != o.code) continue;
        for (int m = NOISE_BSC; m <= NOISE_BURST; m++) {
            if (only_model && (noise_model_t)m != o.model) continue;
            const double *ps = m == NOISE_BSC ? BSC_P : m == NOISE_GE ? GE_PGB : BURST_L;
            int np = m == NOISE_BSC ? COUNT(BSC_P) : m == NOISE_GE ? COUNT(GE_PGB) : COUNT(BURST_L);
            for (int i = 0; i < np; i++) run_point(csv, &o, code, (noise_model_t)m, ps[i]);
        }
    }
    fclose(csv);
    printf("bench: wrote %s\n", path);
    return 0;
}
