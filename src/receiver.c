#include <string.h>
#include "netutil.h"
#include "receiver.h"

int rx_config(uint8_t code, uint8_t rs_t, uint8_t interleave, uint16_t nbits, fec_config_t *out) {
    fec_config_t c = fec_config((code_id_t)code);
    c.rs_t = rs_t;
    c.interleave = interleave;
    if (!fec_config_valid(&c) || fec_coded_bits(&c, PAYLOAD_BITS) != nbits) return -1;
    *out = c;
    return 0;
}

int rx_decode(const fec_config_t *cfg, const uint8_t *bits, size_t nbits, rx_decode_t *out) {
    uint8_t payload[PAYLOAD_BITS];
    fec_result_t res;
    if (!fec_config_valid(cfg) || nbits > PACKET_MAX_BITS ||
        fec_coded_bits(cfg, PAYLOAD_BITS) != nbits)
        return -1;
    out->cfg = *cfg;
    memcpy(out->repaired, bits, nbits);   /* the decoder repairs this copy in place */
    double t0 = now_us();
    fec_decode(cfg, out->repaired, nbits, payload, PAYLOAD_BITS, &res);
    out->crc_ok = frame_parse_payload(payload, out->id);
    out->decode_us = now_us() - t0;
    out->failed_blocks = res.failed_blocks;
    out->corrected_bits = res.corrected_bits;
    return 0;
}

int rx_decode_packet(const packet_t *p, rx_decode_t *out) {
    fec_config_t cfg;
    if (rx_config(p->code, p->rs_t, p->interleave, p->nbits, &cfg) != 0) return -1;
    return rx_decode(&cfg, p->bits, p->nbits, out);   /* p->truth is never passed on */
}

int rx_accept(const rx_decode_t *d) { return d->crc_ok && d->failed_blocks == 0; }

frame_class_t rx_classify(const rx_decode_t *d, const packet_t *p) {
    return frame_classify(d->crc_ok, d->failed_blocks > 0, d->id,
                          p->has_truth ? p->truth.tag : NULL);
}
