#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

void cli_defaults(cli_opts_t *o) {
    memset(o, 0, sizeof *o);
    o->code = CODE_HAMMING74;
    o->profile = &PROFILE_TOLL_PLAZA;
    o->model = PROFILE_TOLL_PLAZA.default_model;
    o->p = -1.0;
    o->seed = 1;
    o->count = 20;
    o->rs_t = 4;
    o->interleave = 1;
    strcpy(o->host, "127.0.0.1");
    o->channel_port = 9000;
    o->tollgate_port = 9001;
    o->feedback_port = 9002;
    o->color = -1;
    o->idle_timeout_ms = 10000;
    o->frames = 5000;
    o->strategy = STRAT_FEC;
    o->rtt_ms = 20;
    o->timeout_ms = 100;
    o->max_retries = 4;
    o->window = 32;
    o->up_threshold = 0.15;
    o->down_threshold = 0.05;
    o->start_level = 1;
}

void cli_usage(FILE *f, const char *prog) {
    fprintf(f,
        "usage: %s [flags]\n"
        "  --code {none,hamming74,hamming1511,secded84,bch157,bch3116,rs}  (default hamming74)\n"
        "  --profile {toll,hospital}   interference profile (default toll)\n"
        "  --model {bsc,ge,burst}      noise model (default: profile's model)\n"
        "  --p <float>                 bsc: flip prob | ge: p_good->bad | burst: burst prob/frame\n"
        "  --burst-len <int>           burst model: burst length L in bits\n"
        "  --seed <int>                RNG seed (default 1)\n"
        "  --count <int>               frames to send (default 20)\n"
        "  --delay-ms <int>            pause between frames (default 0)\n"
        "  --rs-t <int>                Reed-Solomon correctable symbols (default 4)\n"
        "  --interleave <int>          block interleaver depth (default 1 = off)\n"
        "  --host <ip>                 default 127.0.0.1\n"
        "  --channel-port <int>        vehicle -> channel (default 9000)\n"
        "  --tollgate-port <int>       channel -> tollgate (default 9001)\n"
        "  --feedback-port <int>       tollgate -> vehicle (default 9002, week 2)\n"
        "  --color | --no-color        force colours on/off (default: auto)\n"
        "  --show-bits                 tollgate: print the coloured bit map of each damaged frame\n"
        "  --quiet                     no per-frame output\n"
        "  --idle-timeout-ms <int>     stop after this much silence (default 10000, 0 = never)\n"
        "  --log <file>                per-frame CSV log\n"
        "  --summary-file <file>       tollgate: key=value summary\n"
        "  --frames <int> --out <file> bench: frames per point, CSV output\n"
        "week 2:\n"
        "  --strategy {fec,arq,harq,adaptive}  default fec (no feedback)\n"
        "  --rtt-ms <int>              simulated round-trip time (default 20)\n"
        "  --timeout-ms <int>          stop-and-wait timeout (default 100)\n"
        "  --max-retries <int>         retransmissions per frame (default 4)\n"
        "  --fb-drop <float>           tollgate drops this fraction of feedback (failure injection)\n"
        "  --window <int>              adaptive: receiver window W in frames (default 32)\n"
        "  --up-threshold <float>      adaptive: step up if window failure rate > this (0.15)\n"
        "  --down-threshold <float>    adaptive: step down if predicted failure < this (0.05)\n"
        "  --start-level <int>         adaptive: start level 0..5 (default 1)\n"
        "  --schedule <spec>           channel: profile schedule, e.g. toll:200,hospital:200\n"
        "  --strategies [--trace FILE] bench: compare strategies on the --schedule channel\n",
        prog);
}

static int parse_long(const char *flag, const char *s, long lo, long hi, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || *s == '\0' || *end != '\0' || v < lo || v > hi) {
        fprintf(stderr, "error: %s expects an integer in [%ld, %ld], got '%s'\n", flag, lo, hi, s);
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_int(const char *flag, const char *s, long lo, long hi, int *out) {
    long v;
    if (parse_long(flag, s, lo, hi, &v)) return -1;
    *out = (int)v;
    return 0;
}

static int parse_prob(const char *flag, const char *s, double *out) {
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *s == '\0' || *end != '\0' || !(v >= 0.0 && v <= 1.0)) {
        fprintf(stderr, "error: %s expects a probability in [0, 1], got '%s'\n", flag, s);
        return -1;
    }
    *out = v;
    return 0;
}

/* Handles one "--flag value" pair. Returns 0 ok, -1 error. */
static int apply_flag(cli_opts_t *o, const char *f, const char *v) {
    long l;
    if (!strcmp(f, "--code")) {
        if (fec_code_from_name(v, &o->code)) {
            fprintf(stderr, "error: unknown or not yet implemented code '%s'\n", v);
            return -1;
        }
    } else if (!strcmp(f, "--profile")) {
        if (!(o->profile = profile_by_name(v))) {
            fprintf(stderr, "error: unknown profile '%s' (toll, hospital)\n", v);
            return -1;
        }
    } else if (!strcmp(f, "--model")) {
        if (noise_model_from_name(v, &o->model)) {
            fprintf(stderr, "error: unknown model '%s' (bsc, ge, burst)\n", v);
            return -1;
        }
        o->model_set = 1;
    } else if (!strcmp(f, "--p")) {
        return parse_prob(f, v, &o->p);
    } else if (!strcmp(f, "--seed")) {
        if (parse_long(f, v, 0, 2147483647L, &l)) return -1;
        o->seed = (uint64_t)l;
    } else if (!strcmp(f, "--count")) {
        return parse_long(f, v, 0, 100000000L, &o->count);
    } else if (!strcmp(f, "--delay-ms")) {
        return parse_int(f, v, 0, 600000, &o->delay_ms);
    } else if (!strcmp(f, "--burst-len")) {
        return parse_int(f, v, 1, 4096, &o->burst_len);
    } else if (!strcmp(f, "--rs-t")) {
        return parse_int(f, v, 1, 32, &o->rs_t);
    } else if (!strcmp(f, "--interleave")) {
        return parse_int(f, v, 1, 256, &o->interleave);
    } else if (!strcmp(f, "--host")) {
        if (strlen(v) >= sizeof o->host) { fprintf(stderr, "error: host too long\n"); return -1; }
        strcpy(o->host, v);
    } else if (!strcmp(f, "--channel-port")) {
        return parse_int(f, v, 1, 65535, &o->channel_port);
    } else if (!strcmp(f, "--tollgate-port")) {
        return parse_int(f, v, 1, 65535, &o->tollgate_port);
    } else if (!strcmp(f, "--feedback-port")) {
        return parse_int(f, v, 1, 65535, &o->feedback_port);
    } else if (!strcmp(f, "--idle-timeout-ms")) {
        return parse_int(f, v, 0, 86400000, &o->idle_timeout_ms);
    } else if (!strcmp(f, "--log")) {
        o->log_path = v;
    } else if (!strcmp(f, "--summary-file")) {
        o->summary_path = v;
    } else if (!strcmp(f, "--out")) {
        o->out_path = v;
    } else if (!strcmp(f, "--frames")) {
        return parse_long(f, v, 1, 100000000L, &o->frames);
    } else if (!strcmp(f, "--strategy")) {
        if (strategy_from_name(v, &o->strategy)) {
            fprintf(stderr, "error: unknown strategy '%s' (fec, arq, harq, adaptive)\n", v);
            return -1;
        }
    } else if (!strcmp(f, "--rtt-ms")) {
        return parse_int(f, v, 0, 60000, &o->rtt_ms);
    } else if (!strcmp(f, "--timeout-ms")) {
        return parse_int(f, v, 1, 600000, &o->timeout_ms);
    } else if (!strcmp(f, "--max-retries")) {
        return parse_int(f, v, 0, 100, &o->max_retries);
    } else if (!strcmp(f, "--fb-drop")) {
        return parse_prob(f, v, &o->fb_drop);
    } else if (!strcmp(f, "--window")) {
        return parse_int(f, v, 1, 1024, &o->window);
    } else if (!strcmp(f, "--up-threshold")) {
        return parse_prob(f, v, &o->up_threshold);
    } else if (!strcmp(f, "--down-threshold")) {
        return parse_prob(f, v, &o->down_threshold);
    } else if (!strcmp(f, "--start-level")) {
        return parse_int(f, v, 0, 5, &o->start_level);
    } else if (!strcmp(f, "--schedule")) {
        o->schedule = v;
    } else if (!strcmp(f, "--trace")) {
        o->trace_path = v;
    } else {
        fprintf(stderr, "error: unknown flag '%s' (try --help)\n", f);
        return -1;
    }
    return 0;
}

/* Flags that take no value. Returns 1 if handled. */
static int apply_switch(cli_opts_t *o, const char *f) {
    if (!strcmp(f, "--no-color")) o->color = 0;
    else if (!strcmp(f, "--color")) o->color = 1;
    else if (!strcmp(f, "--show-bits")) o->show_bits = 1;
    else if (!strcmp(f, "--quiet")) o->quiet = 1;
    else if (!strcmp(f, "--strategies")) o->strategies = 1;
    else return 0;
    return 1;
}

static int is_value_flag(const char *f) {
    static const char *const FLAGS[] = {
        "--code", "--profile", "--model", "--p", "--seed", "--count", "--delay-ms",
        "--burst-len", "--rs-t", "--interleave", "--host", "--channel-port", "--tollgate-port",
        "--feedback-port", "--idle-timeout-ms", "--log", "--summary-file", "--out", "--frames",
        "--strategy", "--rtt-ms", "--timeout-ms", "--max-retries", "--fb-drop", "--window",
        "--up-threshold", "--down-threshold", "--start-level", "--schedule", "--trace"};
    for (size_t i = 0; i < sizeof FLAGS / sizeof FLAGS[0]; i++)
        if (!strcmp(f, FLAGS[i])) return 1;
    return 0;
}

int cli_parse(cli_opts_t *o, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { cli_usage(stdout, argv[0]); return 1; }
        if (apply_switch(o, a)) continue;
        const char *eq = strchr(a, '=');
        if (eq && !strncmp(a, "--", 2)) {               /* --flag=value */
            char flag[64];
            size_t len = (size_t)(eq - a);
            if (len >= sizeof flag) { fprintf(stderr, "error: bad flag '%s'\n", a); return -1; }
            memcpy(flag, a, len);
            flag[len] = '\0';
            if (apply_flag(o, flag, eq + 1)) return -1;
        } else {                                        /* --flag value */
            if (strncmp(a, "--", 2) != 0) {
                fprintf(stderr, "error: unexpected argument '%s'\n", a);
                return -1;
            }
            if (i + 1 >= argc) {
                if (is_value_flag(a)) fprintf(stderr, "error: %s needs a value\n", a);
                else fprintf(stderr, "error: unknown flag '%s' (try --help)\n", a);
                return -1;
            }
            if (apply_flag(o, a, argv[++i])) return -1;
        }
    }
    if (!o->model_set) o->model = o->profile->default_model;
    return 0;
}

noise_params_t cli_noise_params(const cli_opts_t *o) {
    noise_model_t m = o->model_set ? o->model : o->profile->default_model;
    noise_params_t np = profile_noise(o->profile, m, o->p);
    if (m == NOISE_BURST && o->burst_len > 0) np.burst.length = o->burst_len;
    return np;
}

adapt_params_t cli_adapt_params(const cli_opts_t *o) {
    adapt_params_t a;
    adapt_defaults(&a);
    a.window = o->window;
    a.up_threshold = o->up_threshold;
    a.down_threshold = o->down_threshold;
    a.start_level = o->start_level;
    return a;
}

fec_config_t cli_fec_config(const cli_opts_t *o) {
    fec_config_t c = fec_config(o->code);
    c.rs_t = o->rs_t;
    c.interleave = o->interleave;
    return c;
}
