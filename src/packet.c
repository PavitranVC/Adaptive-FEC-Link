/*
 * packet.c - serialisation and parsing of the UDP datagram that carries a coded frame from
 * vehicle through channel to tollgate (layout in packet.h). Multi-byte fields are big-endian;
 * coded bits are packed MSB-first at the byte boundary and are one bit per byte in packet_t.
 */
#include <string.h>
#include "bits.h"
#include "packet.h"

static void put32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
}
static void put16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)v; }
static uint32_t get32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}
static uint16_t get16(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }

/* Exact datagram length: header + packed bits, plus the sidecar (16 fixed bytes + the packed
 * clean bits) when has_truth. nbits is in bits, everything else in bytes. */
static size_t expected_len(uint16_t nbits, int has_truth) {
    size_t nb = bits_bytes_for(nbits);
    return PACKET_HEADER_BYTES + nb + (has_truth ? 16 + nb : 0);
}

size_t packet_serialize(const packet_t *p, uint8_t *buf, size_t cap) {
    if (p->nbits > PACKET_MAX_BITS) return 0;
    size_t len = expected_len(p->nbits, p->has_truth), nb = bits_bytes_for(p->nbits);
    if (len > cap) return 0;
    buf[0] = 'F'; buf[1] = 'L'; buf[2] = PACKET_VERSION; buf[3] = p->type;
    buf[4] = p->code; buf[5] = p->rs_t; buf[6] = p->interleave;
    buf[7] = p->has_truth ? 1 : 0;
    put32(buf + 8, p->seq);
    put16(buf + 12, p->nbits);
    buf[14] = p->attempt;
    buf[15] = p->strategy;
    bits_pack(p->bits, p->nbits, buf + PACKET_HEADER_BYTES);
    if (p->has_truth) {
        uint8_t *t = buf + PACKET_HEADER_BYTES + nb;
        put16(t, p->truth.flips);
        t[2] = p->truth.profile;
        t[3] = p->truth.model;
        memcpy(t + 4, p->truth.tag, TAG_ID_BYTES);
        bits_pack(p->truth.clean_bits, p->nbits, t + 16);
    }
    return len;
}

/* The length must match exactly (no trailing bytes). Only flag bit0 is interpreted. For a DATA
 * packet the code, rs_t, interleave and strategy must form a valid configuration (an END packet
 * carries no code, so its fields are not checked). The coded size is not compared with the code
 * here (rx_config does that); attempt and the sidecar fields are copied as received. p is not
 * modified when -1 is returned; p->bits beyond nbits is left untouched. */
int packet_parse(const uint8_t *buf, size_t len, packet_t *p) {
    if (len < PACKET_HEADER_BYTES) return -1;
    if (buf[0] != 'F' || buf[1] != 'L' || buf[2] != PACKET_VERSION) return -1;
    if (buf[3] != PKT_DATA && buf[3] != PKT_END) return -1;
    uint16_t nbits = get16(buf + 12);
    int has_truth = buf[7] & 1;
    if (nbits > PACKET_MAX_BITS || len != expected_len(nbits, has_truth)) return -1;
    /* Rejects an unknown code id, an invalid rs_t / interleave depth for that code and an unknown
     * strategy in DATA packets, so no consumer can index a table with a wild header byte. */
    if (buf[3] == PKT_DATA) {
        fec_config_t cfg = fec_config((code_id_t)buf[4]);
        cfg.rs_t = buf[5];
        cfg.interleave = buf[6];
        if (!fec_config_valid(&cfg) || buf[15] > 3) return -1;   /* 3 = adaptive, the last strategy */
    }
    p->type = buf[3]; p->code = buf[4]; p->rs_t = buf[5]; p->interleave = buf[6];
    p->seq = get32(buf + 8);
    p->nbits = nbits;
    p->attempt = buf[14];
    p->strategy = buf[15];
    p->has_truth = has_truth;
    bits_unpack(buf + PACKET_HEADER_BYTES, nbits, p->bits);
    if (has_truth) {
        const uint8_t *t = buf + PACKET_HEADER_BYTES + bits_bytes_for(nbits);
        p->truth.flips = get16(t);
        p->truth.profile = t[2];
        p->truth.model = t[3];
        memcpy(p->truth.tag, t + 4, TAG_ID_BYTES);
        bits_unpack(t + 16, nbits, p->truth.clean_bits);
    }
    return 0;
}

int packet_corrupt_header(uint8_t *buf, size_t len, rng_t *r) {
    if (len < PACKET_HEADER_BYTES) return 0;
    int k = 1 + (int)rng_below(r, PACKET_HEADER_CORRUPT_MAX);
    /* hit[] has one flag per header bit (bit 0 = MSB of byte 0, as in bits_pack). Redrawing
     * until distinct terminates because k <= PACKET_HEADER_CORRUPT_MAX is far below 128. */
    uint8_t hit[8 * PACKET_HEADER_BYTES] = {0};
    for (int done = 0; done < k;) {
        uint32_t bit = rng_below(r, 8 * PACKET_HEADER_BYTES);   /* redraw until distinct */
        if (hit[bit]) continue;
        hit[bit] = 1;
        buf[bit / 8] ^= (uint8_t)(0x80u >> (bit % 8));
        done++;
    }
    return k;
}
