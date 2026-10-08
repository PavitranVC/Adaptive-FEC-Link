#include <string.h>
#include "profiles.h"

const profile_t PROFILE_TOLL_PLAZA = {
    "toll", "TOLL_PLAZA",
    "toll gantry RFID read: mostly random flips, short bursts (modelling assumption)",
    NOISE_BSC,
    0.01,
    {0.002, 0.5, 0.005, 0.5},
    {4, 0.2, 0.5},
};

const profile_t PROFILE_HOSPITAL_IMAGING = {
    "hospital", "HOSPITAL_IMAGING",
    "near MRI/CT/electrosurgical equipment: strong bursty interference (modelling assumption)",
    NOISE_GE,
    0.02,
    {0.005, 0.08, 0.002, 0.5},
    {16, 0.5, 0.5},
};

const profile_t *profile_by_name(const char *name) {
    const profile_t *all[2] = {&PROFILE_TOLL_PLAZA, &PROFILE_HOSPITAL_IMAGING};
    for (int i = 0; i < 2; i++)
        if (strcmp(name, all[i]->name) == 0 || strcmp(name, all[i]->label) == 0) return all[i];
    return NULL;
}

noise_params_t profile_noise(const profile_t *prof, noise_model_t model, double p_override) {
    int ov = p_override >= 0.0;
    switch (model) {
    case NOISE_GE:
        return noise_ge(ov ? p_override : prof->ge.p_gb, prof->ge.p_bg,
                        prof->ge.e_good, prof->ge.e_bad);
    case NOISE_BURST:
        return noise_burst(prof->burst.length, ov ? p_override : prof->burst.p_frame,
                           prof->burst.density);
    case NOISE_BSC:
    default:
        return noise_bsc(ov ? p_override : prof->bsc_p);
    }
}

int profile_id(const profile_t *prof) { return prof == &PROFILE_HOSPITAL_IMAGING ? 1 : 0; }

const profile_t *profile_by_id(int id) {
    if (id == 0) return &PROFILE_TOLL_PLAZA;
    if (id == 1) return &PROFILE_HOSPITAL_IMAGING;
    return NULL;
}
