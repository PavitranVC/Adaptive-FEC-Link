/*
 * frame.h - the toll-booth payload and frame classification.
 *
 *   payload (128 bits) = tag ID (96 bits = 12 bytes) || CRC-32(tag ID) (32 bits, big-endian)
 *
 * The CRC is the receiver's only way to know whether FEC decoding produced the right ID.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>
#include "rng.h"

#define TAG_ID_BYTES 12
#define TAG_ID_BITS 96
#define TAG_HEX_LEN 24
#define PAYLOAD_BITS 128

typedef enum {
    CLASS_CORRECT = 0,       /* CRC ok and ID equals ground truth */
    CLASS_DETECTED_FAIL = 1, /* CRC failed or decoder flagged an uncorrectable block */
    CLASS_SILENT_WRONG = 2,  /* CRC ok but ID differs from ground truth (undetected error) */
    CLASS_UNVERIFIED = 3     /* CRC ok, no ground truth available */
} frame_class_t;

void frame_build_payload(const uint8_t id[TAG_ID_BYTES], uint8_t bits[PAYLOAD_BITS]);

/* Extracts the ID; returns 1 if the received CRC matches CRC-32 of the received ID. */
int frame_parse_payload(const uint8_t bits[PAYLOAD_BITS], uint8_t id[TAG_ID_BYTES]);

/* true_id may be NULL (no ground truth) -> CLASS_UNVERIFIED when the CRC passes. */
frame_class_t frame_classify(int crc_ok, int decoder_failed, const uint8_t *id,
                             const uint8_t *true_id);
const char *frame_class_name(frame_class_t c);

void tag_random(rng_t *r, uint8_t id[TAG_ID_BYTES]);
void tag_to_hex(const uint8_t id[TAG_ID_BYTES], char out[TAG_HEX_LEN + 1]);

#endif /* FRAME_H */
