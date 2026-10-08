/*
 * sim.h - in-process link simulation (no sockets): the same encode -> noise -> decode -> CRC ->
 * classify chain as the UDP pipeline, used by bin/bench for large parameter sweeps.
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>
#include "fec.h"
#include "noise.h"

typedef struct {
    long frames;
    long correct, detected_fail, silent_wrong;
    unsigned long long flips;      /* total bits flipped by the channel */
    double code_rate;              /* effective: 128 / coded bits */
    double mean_decode_us;         /* FEC decode + CRC check per frame */
} sim_result_t;

/* Simulates `frames` frames. Tag IDs and noise are seeded from `seed`, so every code sees the
 * same tag sequence and the same noise process for a given seed. */
void sim_run(const fec_config_t *cfg, const noise_params_t *np, long frames, uint64_t seed,
             sim_result_t *out);

#endif /* SIM_H */
