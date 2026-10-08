/*
 * cli.h - the command-line flags shared by vehicle, channel, tollgate and bench.
 * Every program accepts every flag (it ignores the ones it does not need), so the SAME
 * command line can be given to all three processes - that is how tools/demo.sh works.
 */
#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stdio.h>
#include "fec.h"
#include "noise.h"
#include "profiles.h"

typedef struct {
    /* common flags (FIXED INTERFACE) */
    code_id_t code;            /* --code       default hamming74 */
    const profile_t *profile;  /* --profile    default toll */
    noise_model_t model;       /* --model      default: the profile's default model */
    int model_set;
    double p;                  /* --p          < 0 = profile default */
    uint64_t seed;             /* --seed       default 1 */
    long count;                /* --count      default 20 */
    int delay_ms;              /* --delay-ms   default 0 */
    /* extra flags */
    int burst_len;             /* --burst-len  <= 0 = profile default */
    int rs_t;                  /* --rs-t       default 4 */
    int interleave;            /* --interleave default 1 (off) */
    char host[64];             /* --host       default 127.0.0.1 */
    int channel_port;          /* --channel-port  9000 (vehicle -> channel) */
    int tollgate_port;         /* --tollgate-port 9001 (channel -> tollgate) */
    int feedback_port;         /* --feedback-port 9002 (tollgate -> vehicle, week 2) */
    int color;                 /* --color / --no-color, -1 = auto */
    int show_bits;             /* --show-bits: tollgate prints the coloured bit map */
    int quiet;                 /* --quiet: no per-frame lines */
    int idle_timeout_ms;       /* --idle-timeout-ms (after the first packet), 0 = never */
    const char *log_path;      /* --log FILE: per-frame CSV */
    const char *summary_path;  /* --summary-file FILE: key=value summary */
    const char *out_path;      /* --out FILE (bench) */
    long frames;               /* --frames N (bench) */
} cli_opts_t;

void cli_defaults(cli_opts_t *o);

/* Returns 0 = ok, 1 = --help was printed (exit 0), -1 = error (message on stderr). */
int cli_parse(cli_opts_t *o, int argc, char **argv);

void cli_usage(FILE *f, const char *prog);

/* Noise parameters implied by --profile, --model, --p and --burst-len. */
noise_params_t cli_noise_params(const cli_opts_t *o);

/* FEC configuration implied by --code, --rs-t and --interleave. */
fec_config_t cli_fec_config(const cli_opts_t *o);

#endif /* CLI_H */
