/* Tests for CRC-32 (IEEE 802.3, reflected, poly 0x04C11DB7). */
#include <string.h>
#include "testlib.h"
#include "crc32.h"

TEST(standard_check_value) {
    /* The "check" value from the CRC catalogue for CRC-32/ISO-HDLC. */
    const char *msg = "123456789";
    CHECK_EQ_INT(crc32_compute((const uint8_t *)msg, 9), 0xCBF43926u);
}

TEST(empty_message) {
    CHECK_EQ_INT(crc32_compute(NULL, 0), 0x00000000u);
}

TEST(incremental_equals_one_shot) {
    const uint8_t msg[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint32_t st = crc32_init();
    st = crc32_update(st, msg, 5);
    st = crc32_update(st, msg + 5, 7);
    CHECK_EQ_INT(crc32_final(st), crc32_compute(msg, 12));
}

/* A 96-bit tag ID: CRC-32 must catch every 1- and 2-bit error (Hamming distance >= 4 for
 * messages this short) and every burst of length <= 32. */
TEST(detects_all_single_and_double_bit_errors_in_96_bits) {
    uint8_t id[12] = {0xE2, 0x00, 0x34, 0x12, 0xAB, 0xCD, 0x01, 0x23, 0x45, 0x67, 0x89, 0xEF};
    uint32_t good = crc32_compute(id, 12);
    int missed = 0;
    for (int i = 0; i < 96; i++) {
        id[i / 8] ^= (uint8_t)(0x80 >> (i % 8));
        if (crc32_compute(id, 12) == good) missed++;
        for (int j = i + 1; j < 96; j++) {
            id[j / 8] ^= (uint8_t)(0x80 >> (j % 8));
            if (crc32_compute(id, 12) == good) missed++;
            id[j / 8] ^= (uint8_t)(0x80 >> (j % 8));
        }
        id[i / 8] ^= (uint8_t)(0x80 >> (i % 8));
    }
    CHECK_EQ_INT(missed, 0);
}

TEST(detects_all_bursts_up_to_32_bits) {
    uint8_t id[12] = {0};
    uint32_t good = crc32_compute(id, 12);
    int missed = 0;
    /* burst = first and last bit flipped, a fixed pattern in between */
    for (int len = 1; len <= 32; len++) {
        for (int start = 0; start + len <= 96; start++) {
            uint8_t e[12] = {0};
            for (int b = 0; b < len; b++) {
                int on = (b == 0 || b == len - 1 || (b * 7) % 3 == 0);
                if (on) e[(start + b) / 8] ^= (uint8_t)(0x80 >> ((start + b) % 8));
            }
            uint8_t m[12];
            for (int i = 0; i < 12; i++) m[i] = id[i] ^ e[i];
            if (crc32_compute(m, 12) == good) missed++;
        }
    }
    CHECK_EQ_INT(missed, 0);
}

int main(void) {
    RUN(standard_check_value);
    RUN(empty_message);
    RUN(incremental_equals_one_shot);
    RUN(detects_all_single_and_double_bit_errors_in_96_bits);
    RUN(detects_all_bursts_up_to_32_bits);
    return TEST_REPORT();
}
