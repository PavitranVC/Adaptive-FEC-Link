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
 *  14   1   attempt (0 = first transmission, 1.. = retransmissions)          [version 2]
 *  15   1   strategy (0 fec, 1 arq, 2 harq, 3 adaptive): != fec -> tollgate sends ACK/NAK
 *  16   B   coded bits, packed MSB-first, B = ceil(nbits / 8)        <- the ONLY corrupted part
 *  ---- evaluation-only sidecar, appended by the channel (flags bit0) ----
 *  +0   2   number of flipped bits
 *  +2   1   profile id, +3 1 noise model id
 *  +4  12   true tag ID
 *  +16  B   clean (transmitted) coded bits
 *
 * The header is never corrupted by default (project assumption, decision #1). The failure-injection
 * flag `channel --corrupt-header P` breaks that assumption on purpose (packet_corrupt_header).
 */
#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>
#include "fec.h"
#include "frame.h"
#include "rng.h"

#define PACKET_VERSION 2
#define PACKET_HEADER_BYTES 16
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
    uint8_t attempt;
    uint8_t strategy;
    uint8_t bits[PACKET_MAX_BITS];
    int has_truth;
    truth_t truth;
} packet_t;

/* Returns the number of bytes written, 0 if it does not fit / is invalid. */
size_t packet_serialize(const packet_t *p, uint8_t *buf, size_t cap);

/* Returns 0 on success, -1 on a malformed packet (bad magic/version/type/length). */
int packet_parse(const uint8_t *buf, size_t len, packet_t *p);

/* Failure injection: flips k DISTINCT random bits of the 16-byte header of a serialized packet,
 * k uniform in 1..PACKET_HEADER_CORRUPT_MAX. Returns k (0 if len < header size, nothing done).
 * Payload and sidecar are left alone - the experiment isolates what an unprotected header costs. */
#define PACKET_HEADER_CORRUPT_MAX 4
int packet_corrupt_header(uint8_t *buf, size_t len, rng_t *r);

#endif /* PACKET_H */
