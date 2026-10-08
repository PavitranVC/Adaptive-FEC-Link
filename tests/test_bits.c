/* Tests for bit utilities (include/bits.h). */
#include <string.h>
#include "testlib.h"
#include "bits.h"

TEST(unpack_is_msb_first) {
    uint8_t bytes[1] = {0xA5}; /* 1010 0101 */
    uint8_t bits[8];
    bits_unpack(bytes, 8, bits);
    const uint8_t want[8] = {1, 0, 1, 0, 0, 1, 0, 1};
    CHECK(memcmp(bits, want, 8) == 0);
}

TEST(pack_unpack_roundtrip) {
    uint8_t bytes[16], bits[128], back[16];
    for (int i = 0; i < 16; i++) bytes[i] = (uint8_t)(i * 37 + 11);
    bits_unpack(bytes, 128, bits);
    bits_pack(bits, 128, back);
    CHECK(memcmp(bytes, back, 16) == 0);
}

TEST(pack_pads_partial_byte_with_zeros) {
    const uint8_t bits[3] = {1, 1, 1};
    uint8_t out[1] = {0xFF};
    bits_pack(bits, 3, out);
    CHECK_EQ_INT(out[0], 0xE0);
    CHECK_EQ_INT(bits_bytes_for(3), 1);
    CHECK_EQ_INT(bits_bytes_for(8), 1);
    CHECK_EQ_INT(bits_bytes_for(9), 2);
}

TEST(weight_and_distance) {
    const uint8_t a[6] = {1, 0, 1, 1, 0, 0};
    const uint8_t b[6] = {1, 1, 0, 1, 0, 1};
    CHECK_EQ_INT(bits_weight(a, 6), 3);
    CHECK_EQ_INT(bits_distance(a, b, 6), 3);
    CHECK_EQ_INT(bits_distance(a, a, 6), 0);
}

TEST(to_string) {
    const uint8_t a[5] = {1, 0, 0, 1, 1};
    char s[6];
    bits_to_string(a, 5, s);
    CHECK(strcmp(s, "10011") == 0);
}

int main(void) {
    RUN(unpack_is_msb_first);
    RUN(pack_unpack_roundtrip);
    RUN(pack_pads_partial_byte_with_zeros);
    RUN(weight_and_distance);
    RUN(to_string);
    return TEST_REPORT();
}
