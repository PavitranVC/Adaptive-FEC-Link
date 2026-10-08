#include <stdio.h>
#include <string.h>
#include "bits.h"
#include "crc32.h"
#include "frame.h"

void frame_build_payload(const uint8_t id[TAG_ID_BYTES], uint8_t bits[PAYLOAD_BITS]) {
    uint8_t bytes[TAG_ID_BYTES + 4];
    uint32_t crc = crc32_compute(id, TAG_ID_BYTES);
    memcpy(bytes, id, TAG_ID_BYTES);
    bytes[12] = (uint8_t)(crc >> 24);
    bytes[13] = (uint8_t)(crc >> 16);
    bytes[14] = (uint8_t)(crc >> 8);
    bytes[15] = (uint8_t)crc;
    bits_unpack(bytes, PAYLOAD_BITS, bits);
}

int frame_parse_payload(const uint8_t bits[PAYLOAD_BITS], uint8_t id[TAG_ID_BYTES]) {
    uint8_t bytes[TAG_ID_BYTES + 4];
    bits_pack(bits, PAYLOAD_BITS, bytes);
    memcpy(id, bytes, TAG_ID_BYTES);
    uint32_t rx_crc = ((uint32_t)bytes[12] << 24) | ((uint32_t)bytes[13] << 16) |
                      ((uint32_t)bytes[14] << 8) | (uint32_t)bytes[15];
    return crc32_compute(id, TAG_ID_BYTES) == rx_crc;
}

frame_class_t frame_classify(int crc_ok, int decoder_failed, const uint8_t *id,
                             const uint8_t *true_id) {
    if (!crc_ok || decoder_failed) return CLASS_DETECTED_FAIL;
    if (true_id == NULL) return CLASS_UNVERIFIED;
    return memcmp(id, true_id, TAG_ID_BYTES) == 0 ? CLASS_CORRECT : CLASS_SILENT_WRONG;
}

const char *frame_class_name(frame_class_t c) {
    switch (c) {
    case CLASS_CORRECT:       return "CORRECT";
    case CLASS_DETECTED_FAIL: return "DETECTED_FAIL";
    case CLASS_SILENT_WRONG:  return "SILENT_WRONG";
    case CLASS_UNVERIFIED:    return "UNVERIFIED";
    }
    return "?";
}

void tag_random(rng_t *r, uint8_t id[TAG_ID_BYTES]) {
    for (int i = 0; i < TAG_ID_BYTES; i++) id[i] = (uint8_t)(rng_next(r) >> 56);
}

void tag_to_hex(const uint8_t id[TAG_ID_BYTES], char out[TAG_HEX_LEN + 1]) {
    static const char HEX[] = "0123456789ABCDEF";
    for (int i = 0; i < TAG_ID_BYTES; i++) {
        out[2 * i] = HEX[id[i] >> 4];
        out[2 * i + 1] = HEX[id[i] & 0xF];
    }
    out[TAG_HEX_LEN] = '\0';
}
