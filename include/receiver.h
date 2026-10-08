/*
 * receiver.h - the tollgate's receive path, split in two on purpose:
 *
 *   rx_decode()   FEC decode + CRC check. Its inputs are ONLY the header's code parameters and
 *                 the received (corrupted) coded bits - it has no access to the channel's
 *                 ground-truth sidecar, so it cannot cheat (decision #6, tested in
 *                 tests/test_receiver.c).
 *   rx_classify() Evaluation only: compares the decoded result with the sidecar's true tag to
 *                 label the frame CORRECT / DETECTED_FAIL / SILENT_WRONG (UNVERIFIED if absent).
 */
#ifndef RECEIVER_H
#define RECEIVER_H

#include <stddef.h>
#include <stdint.h>
#include "fec.h"
#include "frame.h"
#include "packet.h"

typedef struct {
    fec_config_t cfg;
    int crc_ok;
    int failed_blocks;                 /* blocks where the decoder detected a failure */
    int corrected_bits;                /* bits the decoder changed */
    uint8_t id[TAG_ID_BYTES];          /* decoded tag ID */
    uint8_t repaired[PACKET_MAX_BITS]; /* decoder output codeword, transmission order */
    double decode_us;                  /* FEC decode + CRC check time */
} rx_decode_t;

/* Decoder config from the (uncorrupted) header; returns -1 if unknown / size mismatch. */
int rx_config(uint8_t code, uint8_t rs_t, uint8_t interleave, uint16_t nbits, fec_config_t *out);

/* Decode received bits. Returns 0 ok, -1 invalid configuration. */
int rx_decode(const fec_config_t *cfg, const uint8_t *bits, size_t nbits, rx_decode_t *out);

/* Convenience: rx_config + rx_decode using ONLY header fields and p->bits. */
int rx_decode_packet(const packet_t *p, rx_decode_t *out);

/* ACK-worthy: CRC passed and no block flagged a failure. */
int rx_accept(const rx_decode_t *d);

/* Classification against ground truth (the only function that reads p->truth). */
frame_class_t rx_classify(const rx_decode_t *d, const packet_t *p);

#endif /* RECEIVER_H */
