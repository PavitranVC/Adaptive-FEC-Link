#include <string.h>
#include "frame.h"
#include "netutil.h"
#include "sim.h"

void sim_run(const fec_config_t *cfg, const noise_params_t *np, long frames, uint64_t seed,
             sim_result_t *out) {
    uint8_t id[TAG_ID_BYTES], rx_id[TAG_ID_BYTES], payload[PAYLOAD_BITS], decoded[PAYLOAD_BITS];
    uint8_t coded[FEC_MAX_CODED_BITS];
    rng_t tags;
    noise_t ch;
    rng_seed(&tags, rng_derive(seed, "sim-tags"));
    noise_init(&ch, np, rng_derive(seed, "sim-noise"));
    memset(out, 0, sizeof *out);
    out->code_rate = fec_code_rate(cfg, PAYLOAD_BITS);
    double total_us = 0.0;

    for (long f = 0; f < frames; f++) {
        tag_random(&tags, id);
        frame_build_payload(id, payload);
        size_t n = fec_encode(cfg, payload, PAYLOAD_BITS, coded);
        out->flips += noise_apply(&ch, coded, n, NULL);

        fec_result_t res;
        double t0 = now_us();
        fec_decode(cfg, coded, n, decoded, PAYLOAD_BITS, &res);
        int crc_ok = frame_parse_payload(decoded, rx_id);
        total_us += now_us() - t0;

        switch (frame_classify(crc_ok, res.failed_blocks > 0, rx_id, id)) {
        case CLASS_CORRECT:       out->correct++; break;
        case CLASS_SILENT_WRONG:  out->silent_wrong++; break;
        default:                  out->detected_fail++; break;
        }
    }
    out->frames = frames;
    out->mean_decode_us = frames ? total_us / (double)frames : 0.0;
}
