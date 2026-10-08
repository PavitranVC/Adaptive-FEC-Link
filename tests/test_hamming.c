/* Tests for Hamming(7,4) and Hamming(15,11). */
#include <string.h>
#include "testlib.h"
#include "bits.h"
#include "crc32.h"
#include "hamming.h"

static void int_to_bits(unsigned v, int k, uint8_t *bits) {
    for (int i = 0; i < k; i++) bits[i] = (uint8_t)((v >> (k - 1 - i)) & 1u);
}

/* Generic exhaustive single-error test for Hamming(2^r - 1, 2^r - 1 - r). */
static int check_all_single_errors(int r) {
    int n = (1 << r) - 1, k = n - r, bad = 0;
    uint8_t data[16], code[16], rx[16], out[16];
    for (unsigned v = 0; v < (1u << k); v++) {
        int_to_bits(v, k, data);
        hamming_encode(r, data, code);
        if (hamming_syndrome(r, code) != 0) bad++;           /* valid codeword */
        if (hamming_decode(r, code, out) != 0) bad++;
        for (int pos = 0; pos < n; pos++) {                   /* every position */
            memcpy(rx, code, (size_t)n);
            rx[pos] ^= 1;
            if (hamming_syndrome(r, rx) != pos + 1) bad++;    /* syndrome = position */
            if (hamming_decode(r, rx, out) != 1) bad++;
            if (memcmp(out, data, (size_t)k) != 0) bad++;
            if (memcmp(rx, code, (size_t)n) != 0) bad++;      /* codeword repaired */
        }
    }
    return bad;
}

TEST(hamming74_parameters) {
    CHECK_EQ_INT(hamming_n(3), 7);
    CHECK_EQ_INT(hamming_k(3), 4);
    CHECK_EQ_INT(hamming_n(4), 15);
    CHECK_EQ_INT(hamming_k(4), 11);
}

TEST(hamming74_known_codeword) {
    /* data 1011 -> positions p1 p2 d1 p4 d2 d3 d4 = 0 1 1 0 0 1 1 */
    const uint8_t data[4] = {1, 0, 1, 1};
    const uint8_t want[7] = {0, 1, 1, 0, 0, 1, 1};
    uint8_t code[7];
    hamming_encode(3, data, code);
    CHECK(memcmp(code, want, 7) == 0);
}

TEST(hamming74_corrects_every_single_bit_error) { CHECK_EQ_INT(check_all_single_errors(3), 0); }
TEST(hamming1511_corrects_every_single_bit_error) { CHECK_EQ_INT(check_all_single_errors(4), 0); }

/* A 2-bit error in a perfect Hamming code always produces a non-zero syndrome that points at a
 * THIRD position: the decoder "corrects" it and ends up 3 bits away -> wrong data, no warning. */
TEST(hamming74_two_bit_error_miscorrects) {
    uint8_t data[4], code[7], rx[7], out[4];
    int wrong = 0, total = 0;
    for (unsigned v = 0; v < 16; v++) {
        int_to_bits(v, 4, data);
        hamming_encode(3, data, code);
        for (int i = 0; i < 7; i++)
            for (int j = i + 1; j < 7; j++) {
                memcpy(rx, code, 7);
                rx[i] ^= 1; rx[j] ^= 1;
                int ret = hamming_decode(3, rx, out);
                total++;
                if (ret == 1 && memcmp(out, data, 4) != 0) wrong++;
            }
    }
    CHECK_EQ_INT(wrong, total); /* every double error is mis-corrected */
}

/* ...and the CRC-32 over the tag ID catches it. Frame = 96-bit ID + 32-bit CRC = 128 bits =
 * 32 Hamming(7,4) blocks. Any 2-bit error inside one block becomes a 3-bit codeword error,
 * touching at most 4 consecutive message bits: a burst <= 32, which CRC-32 always detects. */
TEST(crc32_catches_hamming74_miscorrection) {
    const uint8_t id[12] = {0xE2, 0x80, 0x11, 0x60, 0x20, 0x00, 0x7A, 0x5B, 0x01, 0x9C, 0x33, 0xF0};
    uint8_t msg[16], bits[128], coded[32 * 7], rx[32 * 7], dec[128], back[16];
    memcpy(msg, id, 12);
    uint32_t crc = crc32_compute(id, 12);
    msg[12] = (uint8_t)(crc >> 24); msg[13] = (uint8_t)(crc >> 16);
    msg[14] = (uint8_t)(crc >> 8);  msg[15] = (uint8_t)crc;
    bits_unpack(msg, 128, bits);
    for (int b = 0; b < 32; b++) hamming_encode(3, bits + 4 * b, coded + 7 * b);

    int caught = 0, cases = 0;
    for (int blk = 0; blk < 32; blk++)
        for (int i = 0; i < 7; i++)
            for (int j = i + 1; j < 7; j++) {
                memcpy(rx, coded, sizeof rx);
                rx[7 * blk + i] ^= 1; rx[7 * blk + j] ^= 1;
                for (int b = 0; b < 32; b++) hamming_decode(3, rx + 7 * b, dec + 4 * b);
                bits_pack(dec, 128, back);
                uint32_t rx_crc = ((uint32_t)back[12] << 24) | ((uint32_t)back[13] << 16) |
                                  ((uint32_t)back[14] << 8) | back[15];
                cases++;
                if (crc32_compute(back, 12) != rx_crc) caught++;
            }
    CHECK_EQ_INT(caught, cases);
}

int main(void) {
    RUN(hamming74_parameters);
    RUN(hamming74_known_codeword);
    RUN(hamming74_corrects_every_single_bit_error);
    RUN(hamming1511_corrects_every_single_bit_error);
    RUN(hamming74_two_bit_error_miscorrects);
    RUN(crc32_catches_hamming74_miscorrection);
    return TEST_REPORT();
}
