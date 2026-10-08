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

int packet_parse(const uint8_t *buf, size_t len, packet_t *p) {
    if (len < PACKET_HEADER_BYTES) return -1;
    if (buf[0] != 'F' || buf[1] != 'L' || buf[2] != PACKET_VERSION) return -1;
    if (buf[3] != PKT_DATA && buf[3] != PKT_END) return -1;
    uint16_t nbits = get16(buf + 12);
    int has_truth = buf[7] & 1;
    if (nbits > PACKET_MAX_BITS || len != expected_len(nbits, has_truth)) return -1;
    p->type = buf[3]; p->code = buf[4]; p->rs_t = buf[5]; p->interleave = buf[6];
    p->seq = get32(buf + 8);
    p->nbits = nbits;
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
