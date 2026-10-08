/*
 * crc32.h - CRC-32 as used by Ethernet / zip (CRC-32/ISO-HDLC).
 *
 * Generator polynomial G(x) = x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8
 *                           + x^7 + x^5 + x^4 + x^2 + x + 1          (0x04C11DB7)
 * processed bit-reflected (0xEDB88320), initial register 0xFFFFFFFF, final XOR 0xFFFFFFFF.
 *
 * Why it protects the tag ID: the CRC is the remainder of M(x)*x^32 divided by G(x). An error
 * E(x) is missed only if G(x) divides E(x). G has a non-zero x^0 term and degree 32, so it cannot
 * divide any burst of length <= 32; for messages as short as ours (128 bits) its minimum Hamming
 * distance is >= 4, so every 1-, 2- and 3-bit error is detected.
 */
#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t state, const uint8_t *data, size_t len);
uint32_t crc32_final(uint32_t state);

/* One-shot: crc32_final(crc32_update(crc32_init(), data, len)). */
uint32_t crc32_compute(const uint8_t *data, size_t len);

#endif /* CRC32_H */
