#include <stdlib.h>
#include <string.h>
#include "schedule.h"

/* Parses one "name:count" item of length len. */
static int parse_item(const char *item, size_t len, sched_seg_t *out) {
    char buf[64], *colon, *end;
    if (len == 0 || len >= sizeof buf) return -1;
    memcpy(buf, item, len);
    buf[len] = '\0';
    if (!(colon = strchr(buf, ':'))) return -1;
    *colon = '\0';
    const profile_t *p = profile_by_name(buf);
    long n = strtol(colon + 1, &end, 10);
    if (!p || colon[1] == '\0' || *end != '\0' || n <= 0) return -1;
    out->profile = p;
    out->frames = n;
    return 0;
}

int schedule_parse(const char *spec, schedule_t *out) {
    memset(out, 0, sizeof *out);
    if (!spec || !*spec) return -1;
    const char *p = spec;
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (out->n == SCHEDULE_MAX || parse_item(p, len, &out->seg[out->n]) != 0) return -1;
        out->n++;
        if (!comma) return 0;
        p = comma + 1;
    }
}

int schedule_segment_at(const schedule_t *s, long tx) {
    long end = 0;
    for (int i = 0; i < s->n - 1; i++) {
        end += s->seg[i].frames;
        if (tx < end) return i;
    }
    return s->n - 1;
}

long schedule_total(const schedule_t *s) {
    long t = 0;
    for (int i = 0; i < s->n; i++) t += s->seg[i].frames;
    return t;
}

noise_params_t schedule_noise(const schedule_t *s, int i, int model_set, noise_model_t model, double p) {
    const profile_t *prof = s->seg[i].profile;
    return profile_noise(prof, model_set ? model : prof->default_model, p);
}

void schedule_to_linksim(const schedule_t *s, int model_set, noise_model_t model, double p,
                         linksim_params_t *lp) {
    lp->nseg = s->n;
    for (int i = 0; i < s->n; i++) {
        lp->seg[i].noise = schedule_noise(s, i, model_set, model, p);
        lp->seg[i].frames = s->seg[i].frames;
        lp->seg[i].label = s->seg[i].profile->label;
    }
}
