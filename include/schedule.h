/*
 * schedule.h - a channel whose interference profile changes over time:
 *     --schedule "toll:200,hospital:200,toll:200"
 * means 200 transmissions with TOLL_PLAZA noise, then 200 with HOSPITAL_IMAGING, then TOLL_PLAZA
 * again. Counted in TRANSMISSIONS (air time), so retransmissions also move the schedule
 * forward. After the last segment the last profile stays in force.
 */
#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "linksim.h"
#include "profiles.h"

#define SCHEDULE_MAX LINKSIM_MAX_SEG

typedef struct {
    const profile_t *profile;
    long frames;
} sched_seg_t;

typedef struct {
    int n;
    sched_seg_t seg[SCHEDULE_MAX];
} schedule_t;

int schedule_parse(const char *spec, schedule_t *out);   /* 0 ok, -1 malformed */
int schedule_segment_at(const schedule_t *s, long tx);   /* segment index for transmission tx */
long schedule_total(const schedule_t *s);

/* Noise parameters of segment i (profile default model unless model_set; p as in --p). */
noise_params_t schedule_noise(const schedule_t *s, int i, int model_set, noise_model_t model, double p);

/* Fill lp->seg / lp->nseg from the schedule. */
void schedule_to_linksim(const schedule_t *s, int model_set, noise_model_t model, double p,
                         linksim_params_t *lp);

#endif /* SCHEDULE_H */
