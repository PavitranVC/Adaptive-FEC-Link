/*
 * profiles.h - interference profiles (presets for the noise models).
 *
 * ALL NUMBERS HERE ARE MODELLING ASSUMPTIONS chosen to be plausible and to make the codes'
 * differences visible - they are not measurements. Change them here, in one place.
 *
 * TOLL_PLAZA (primary use case): a UHF RFID read at a toll gantry. Thermal noise and multipath
 *   give mostly independent random flips; passing trucks / other readers cause short bursts.
 *     BSC   p = 0.01
 *     GE    p_gb = 0.002, p_bg = 0.5 (mean burst 2 bits), e_good = 0.005, e_bad = 0.5
 *           -> average BER ~ 0.007
 *     BURST L = 4 bits, in 20 % of frames, density 0.5
 *
 * HOSPITAL_IMAGING (secondary): the same link next to MRI gradient coils, CT gantries or
 *   electrosurgical units, which emit strong impulsive/bursty interference.
 *     BSC   p = 0.02
 *     GE    p_gb = 0.005, p_bg = 0.08 (mean burst 12.5 bits), e_good = 0.002, e_bad = 0.5
 *           -> average BER ~ 0.031
 *     BURST L = 16 bits, in 50 % of frames, density 0.5
 */
#ifndef PROFILES_H
#define PROFILES_H

#include "noise.h"

typedef struct {
    const char *name;         /* CLI name: "toll" / "hospital" */
    const char *label;        /* "TOLL_PLAZA" / "HOSPITAL_IMAGING" */
    const char *description;
    noise_model_t default_model;
    double bsc_p;
    ge_params_t ge;
    burst_params_t burst;
} profile_t;

extern const profile_t PROFILE_TOLL_PLAZA;
extern const profile_t PROFILE_HOSPITAL_IMAGING;

/* "toll" or "hospital" (also accepts the labels); NULL if unknown. */
const profile_t *profile_by_name(const char *name);

/* Noise parameters of `model` from the profile. If p_override >= 0 it replaces the model's
 * main knob:  BSC -> p,  GE -> p_gb (how often bursts start),  BURST -> p_frame. */
noise_params_t profile_noise(const profile_t *prof, noise_model_t model, double p_override);

#endif /* PROFILES_H */
