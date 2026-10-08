/* Tests for the block interleaver and its effect on burst errors. */
#include <string.h>
#include "testlib.h"
#include "fec.h"
#include "interleave.h"

TEST(identity_when_depth_1) {
    uint8_t in[50], out[50];
    for (int i = 0; i < 50; i++) in[i] = (uint8_t)(i & 1 ? i % 3 == 0 : 1);
    interleave(in, out, 50, 1);
    CHECK(memcmp(in, out, 50) == 0);
}

TEST(roundtrip_for_many_sizes_and_depths) {
    uint8_t in[300], mid[300], back[300];
    int bad = 0;
    for (int n = 1; n <= 300; n += 7)
        for (int d = 1; d <= 40; d++) {
            for (int i = 0; i < n; i++) in[i] = (uint8_t)((i * 31 + d) % 5 == 0);
            interleave(in, mid, (size_t)n, d);
            deinterleave(mid, back, (size_t)n, d);
            if (memcmp(in, back, (size_t)n)) bad++;
        }
    CHECK_EQ_INT(bad, 0);
}

TEST(is_a_permutation_with_known_order) {
    /* n = 6, depth 2 -> matrix 2 x 3 written by rows [0 1 2 / 3 4 5], read by columns */
    uint8_t in[6] = {0, 1, 2, 3, 4, 5}, out[6];
    interleave(in, out, 6, 2);
    const uint8_t want[6] = {0, 3, 1, 4, 2, 5};
    CHECK(memcmp(out, want, 6) == 0);
}

/* Hamming(7,4): 224 coded bits. Without interleaving a 2-bit burst usually breaks a block;
 * with depth 8 (8 x 28 matrix) EVERY channel burst of up to 8 bits lands in 8 different blocks. */
TEST(hamming74_with_depth8_survives_every_burst_up_to_8) {
    fec_config_t plain = fec_config(CODE_HAMMING74), il = plain;
    il.interleave = 8;
    uint8_t pay[128], coded[FEC_MAX_CODED_BITS], out[128];
    for (int i = 0; i < 128; i++) pay[i] = (uint8_t)((i * 13) % 7 < 3);
    int bad_il = 0, bad_plain = 0;
    for (int L = 1; L <= 8; L++)
        for (int start = 0; start + L <= 224; start++) {
            fec_result_t res;
            size_t n = fec_encode(&il, pay, 128, coded);
            for (int b = start; b < start + L; b++) coded[b] ^= 1;
            fec_decode(&il, coded, n, out, 128, &res);
            if (memcmp(out, pay, 128)) bad_il++;
            n = fec_encode(&plain, pay, 128, coded);
            for (int b = start; b < start + L; b++) coded[b] ^= 1;
            fec_decode(&plain, coded, n, out, 128, &res);
            if (memcmp(out, pay, 128)) bad_plain++;
        }
    CHECK_EQ_INT(bad_il, 0);
    CHECK(bad_plain > 1000);
}

TEST(decode_returns_repaired_word_in_transmission_order) {
    fec_config_t il = fec_config(CODE_BCH157);
    il.interleave = 15;                          /* 285 = 15 x 19 */
    uint8_t pay[128] = {0}, coded[FEC_MAX_CODED_BITS], clean[FEC_MAX_CODED_BITS], out[128];
    pay[5] = pay[77] = 1;
    size_t n = fec_encode(&il, pay, 128, coded);
    memcpy(clean, coded, n);
    for (int b = 40; b < 52; b++) coded[b] ^= 1; /* 12-bit burst */
    fec_result_t res;
    fec_decode(&il, coded, n, out, 128, &res);
    CHECK(memcmp(out, pay, 128) == 0);
    CHECK(memcmp(coded, clean, n) == 0);
    CHECK_EQ_INT(res.corrected_bits, 12);
}

int main(void) {
    RUN(identity_when_depth_1);
    RUN(roundtrip_for_many_sizes_and_depths);
    RUN(is_a_permutation_with_known_order);
    RUN(hamming74_with_depth8_survives_every_burst_up_to_8);
    RUN(decode_returns_repaired_word_in_transmission_order);
    return TEST_REPORT();
}
