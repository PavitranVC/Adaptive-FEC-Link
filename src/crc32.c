/*
 * Bitwise CRC-32 (reflected). One byte at a time:
 *   reg ^= byte;  repeat 8x: if (reg & 1) reg = (reg >> 1) ^ 0xEDB88320 else reg >>= 1
 * This is polynomial long division over GF(2) done LSB-first; the "XOR with the polynomial"
 * step is subtraction modulo 2. A 256-entry table would be faster but hides the maths; our
 * messages are only 12 bytes long.
 */
#include "crc32.h"

#define CRC32_POLY_REFLECTED 0xEDB88320u

uint32_t crc32_init(void) { return 0xFFFFFFFFu; }

uint32_t crc32_update(uint32_t reg, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        reg ^= data[i];
        for (int b = 0; b < 8; b++)
            reg = (reg & 1u) ? (reg >> 1) ^ CRC32_POLY_REFLECTED : (reg >> 1);
    }
    return reg;
}

uint32_t crc32_final(uint32_t reg) { return reg ^ 0xFFFFFFFFu; }

uint32_t crc32_compute(const uint8_t *data, size_t len) {
    return crc32_final(crc32_update(crc32_init(), data, len));
}
