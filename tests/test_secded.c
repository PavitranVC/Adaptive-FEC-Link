/* Tests for extended Hamming SECDED(8,4). */
#include <string.h>
#include "testlib.h"
#include "secded.h"

static void int_to_bits(unsigned v, uint8_t *bits) {
    for (int i = 0; i < 4; i++) bits[i] = (uint8_t)((v >> (3 - i)) & 1u);
}

TEST(clean_codeword_decodes) {
    uint8_t d[4], c[8], out[4];
    for (unsigned v = 0; v < 16; v++) {
        int_to_bits(v, d);
        secded84_encode(d, c);
        CHECK_EQ_INT(secded84_decode(c, out), SECDED_OK);
        CHECK(memcmp(out, d, 4) == 0);
    }
}

TEST(minimum_distance_is_4) {
    uint8_t a[4], b[4], ca[8], cb[8];
    int dmin = 99;
    for (unsigned u = 0; u < 16; u++)
        for (unsigned v = u + 1; v < 16; v++) {
            int_to_bits(u, a); int_to_bits(v, b);
            secded84_encode(a, ca); secded84_encode(b, cb);
            int d = 0;
            for (int i = 0; i < 8; i++) d += ca[i] != cb[i];
            if (d < dmin) dmin = d;
        }
    CHECK_EQ_INT(dmin, 4);
}

TEST(corrects_every_single_bit_error) {
    uint8_t d[4], c[8], rx[8], out[4];
    for (unsigned v = 0; v < 16; v++) {
        int_to_bits(v, d);
        secded84_encode(d, c);
        for (int pos = 0; pos < 8; pos++) {           /* includes the overall parity bit */
            memcpy(rx, c, 8);
            rx[pos] ^= 1;
            CHECK_EQ_INT(secded84_decode(rx, out), SECDED_CORRECTED);
            CHECK(memcmp(out, d, 4) == 0);
            CHECK(memcmp(rx, c, 8) == 0);
        }
    }
}

TEST(detects_every_double_bit_error) {
    uint8_t d[4], c[8], rx[8], out[4];
    int detected = 0, total = 0;
    for (unsigned v = 0; v < 16; v++) {
        int_to_bits(v, d);
        secded84_encode(d, c);
        for (int i = 0; i < 8; i++)
            for (int j = i + 1; j < 8; j++) {
                memcpy(rx, c, 8);
                rx[i] ^= 1; rx[j] ^= 1;
                total++;
                if (secded84_decode(rx, out) == SECDED_DOUBLE) detected++;
            }
    }
    CHECK_EQ_INT(detected, total);
}

int main(void) {
    RUN(clean_codeword_decodes);
    RUN(minimum_distance_is_4);
    RUN(corrects_every_single_bit_error);
    RUN(detects_every_double_bit_error);
    return TEST_REPORT();
}
