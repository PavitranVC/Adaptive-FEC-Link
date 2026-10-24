/*
 * bits.c - conversion between the one-bit-per-byte vectors used inside the coding layer and
 * the packed byte arrays used on the wire (packet.c) and in the payload (frame.c).
 */
#include "bits.h"

size_t bits_bytes_for(size_t nbits) { return (nbits + 7) / 8; }

/* Bit i is byte i/8, mask 0x80 >> (i%8): MSB first. Output elements are strictly 0 or 1.
 * Reads ceil(nbits/8) bytes; any padding bits in the last byte are ignored. */
void bits_unpack(const uint8_t *bytes, size_t nbits, uint8_t *bits) {
    for (size_t i = 0; i < nbits; i++)
        bits[i] = (uint8_t)((bytes[i / 8] >> (7 - i % 8)) & 1u);
}

/* Any non-zero input element counts as 1. Zeroes all ceil(nbits/8) output bytes first, so the
 * unused low bits of the last byte are always 0. */
void bits_pack(const uint8_t *bits, size_t nbits, uint8_t *bytes) {
    size_t nbytes = bits_bytes_for(nbits);
    for (size_t i = 0; i < nbytes; i++) bytes[i] = 0;
    for (size_t i = 0; i < nbits; i++)
        if (bits[i]) bytes[i / 8] |= (uint8_t)(0x80u >> (i % 8));
}

/* weight and distance treat any non-zero element as 1, matching bits_pack. */
size_t bits_weight(const uint8_t *bits, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n; i++) w += bits[i] != 0;
    return w;
}

size_t bits_distance(const uint8_t *a, const uint8_t *b, size_t n) {
    size_t d = 0;
    for (size_t i = 0; i < n; i++) d += (a[i] != 0) != (b[i] != 0);
    return d;
}

void bits_to_string(const uint8_t *bits, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) out[i] = bits[i] ? '1' : '0';
    out[n] = '\0';
}
