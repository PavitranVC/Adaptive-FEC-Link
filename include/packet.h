/*
 * packet.h - UDP wire format between vehicle, channel and tollgate.
 *
 *  off size field
 *   0   2   magic "FL"
 *   2   1   version (1)
 *   3   1   type: 1 = DATA, 2 = END (seq = number of frames sent)
 *   4   1   code id (see fec.h)
 *   5   1   rs_t (Reed-Solomon parameter)
 *   6   1   interleaver depth
 *   7   1   flags: bit0 = ground-truth sidecar present
 *   8   4   seq (big-endian)
 *  12   2   nbits = number of coded bits (big-endian)
 *  14   B   coded bits, packed MSB-first, B = ceil(nbits / 8)        <- the ONLY corrupted part
 *  ---- evaluation-only sidecar, appended by the channel (flags bit0) ----
 *  +0   2   number of flipped bits
 *  +2   1   profile id, +3 1 noise model id
 *  +4  12   true tag ID
 *  +16  B   clean (transmitted) coded bits
 *
 * The header is never corrupted (project assumption, see DECISIONS.md).
 */
#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>
#include "fec.h"
#include "frame.h"

#define PACKET_VERSION 1
#define PACKET_HEADER_BYTES 14
#define PACKET_MAX_BITS FEC_MAX_CODED_BITS
#define PACKET_MAX_BYTES (PACKET_HEADER_BYTES + 16 + 2 * (PACKET_MAX_BITS / 8))

enum { PKT_DATA = 1, PKT_END = 2 };

typedef struct {
    uint16_t flips;
    uint8_t profile;
    uint8_t model;
    uint8_t tag[TAG_ID_BYTES];
    uint8_t clean_bits[PACKET_MAX_BITS];
} truth_t;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint8_t rs_t;
    uint8_t interleave;
    uint32_t seq;
    uint16_t nbits;
    uint8_t bits[PACKET_MAX_BITS];
    int has_truth;
    truth_t truth;
} packet_t;

/* Returns the number of bytes written, 0 if it does not fit / is invalid. */
size_t packet_serialize(const packet_t *p, uint8_t *buf, size_t cap);

/* Returns 0 on success, -1 on a malformed packet (bad magic/version/type/length). */
int packet_parse(const uint8_t *buf, size_t len, packet_t *p);

#endif /* PACKET_H */
