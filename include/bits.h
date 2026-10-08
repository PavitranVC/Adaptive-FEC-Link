/*
 * bits.h - bit-vector helpers.
 *
 * Inside the coding layer a bit vector is an array of uint8_t holding 0 or 1 (one bit per byte).
 * That wastes memory but makes every encoder/decoder read like the maths on the slides.
 * Packing to real bytes (MSB first) happens only at the network boundary.
 */
#ifndef BITS_H
#define BITS_H

#include <stddef.h>
#include <stdint.h>

/* Number of bytes needed to hold nbits packed bits. */
size_t bits_bytes_for(size_t nbits);

/* bytes -> one-bit-per-byte vector, MSB of bytes[0] first. Reads ceil(nbits/8) bytes. */
void bits_unpack(const uint8_t *bytes, size_t nbits, uint8_t *bits);

/* one-bit-per-byte vector -> bytes, MSB first; unused low bits of the last byte are 0. */
void bits_pack(const uint8_t *bits, size_t nbits, uint8_t *bytes);

/* Hamming weight (number of ones). */
size_t bits_weight(const uint8_t *bits, size_t n);

/* Hamming distance (number of positions where a and b differ). */
size_t bits_distance(const uint8_t *a, const uint8_t *b, size_t n);

/* Writes n characters '0'/'1' plus a terminating NUL into out (size >= n + 1). */
void bits_to_string(const uint8_t *bits, size_t n, char *out);

#endif /* BITS_H */
