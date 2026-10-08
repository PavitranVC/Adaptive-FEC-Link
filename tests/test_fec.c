/* Tests for the frame-level FEC layer (include/fec.h): every registered code must round-trip
 * random payloads and correct one error in every block. */
#include <string.h>
#include "testlib.h"
#include "bits.h"
#include "fec.h"
#include "rng.h"

#define K PAYLOAD_BITS_DEFAULT

static void random_bits(rng_t *r, uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(rng_next(r) & 1u);
}

TEST(names_roundtrip) {
    for (int i = 0; i < fec_code_count(); i++) {
        const fec_code_info_t *info = fec_code_at(i);
        code_id_t id;
        CHECK(fec_code_from_name(info->name, &id) == 0);
        CHECK(id == info->id);
        CHECK(strcmp(fec_code_name(id), info->name) == 0);
    }
    code_id_t id;
    CHECK(fec_code_from_name("turbo", &id) != 0);
}

TEST(coded_sizes) {
    fec_config_t c = fec_config(CODE_HAMMING74);
    CHECK_EQ_INT(fec_coded_bits(&c, 128), 32 * 7);
    c = fec_config(CODE_HAMMING1511);
    CHECK_EQ_INT(fec_coded_bits(&c, 128), 12 * 15);     /* ceil(128/11) = 12 blocks, padded */
    c = fec_config(CODE_SECDED84);
    CHECK_EQ_INT(fec_coded_bits(&c, 128), 32 * 8);
    c = fec_config(CODE_NONE);
    CHECK_EQ_INT(fec_coded_bits(&c, 128), 128);
    c = fec_config(CODE_HAMMING74);
    CHECK_NEAR(fec_code_rate(&c, 128), 4.0 / 7.0, 1e-12);
}

TEST(all_codes_roundtrip_clean) {
    rng_t r;
    rng_seed(&r, 1);
    for (int i = 0; i < fec_code_count(); i++) {
        fec_config_t c = fec_config(fec_code_at(i)->id);
        for (int trial = 0; trial < 200; trial++) {
            uint8_t pay[K], coded[FEC_MAX_CODED_BITS], out[K];
            random_bits(&r, pay, K);
            size_t n = fec_encode(&c, pay, K, coded);
            fec_result_t res;
            fec_decode(&c, coded, n, out, K, &res);
            CHECK(memcmp(pay, out, K) == 0);
            CHECK_EQ_INT(res.corrected_bits, 0);
            CHECK_EQ_INT(res.failed_blocks, 0);
        }
    }
}

/* One flipped bit in every block at once: all codes with t >= 1 repair all of them. */
TEST(all_codes_fix_one_error_per_block) {
    rng_t r;
    rng_seed(&r, 2);
    for (int i = 0; i < fec_code_count(); i++) {
        const fec_code_info_t *info = fec_code_at(i);
        if (info->t < 1) continue;
        fec_config_t c = fec_config(info->id);
        for (int trial = 0; trial < 200; trial++) {
            uint8_t pay[K], coded[FEC_MAX_CODED_BITS], clean[FEC_MAX_CODED_BITS], out[K];
            random_bits(&r, pay, K);
            size_t n = fec_encode(&c, pay, K, coded);
            memcpy(clean, coded, n);
            int bn = fec_block_n(&c), blocks = (int)(n / (size_t)bn);
            for (int b = 0; b < blocks; b++) coded[b * bn + rng_below(&r, (uint32_t)bn)] ^= 1;
            fec_result_t res;
            fec_decode(&c, coded, n, out, K, &res);
            CHECK(memcmp(pay, out, K) == 0);
            CHECK_EQ_INT(res.corrected_bits, blocks);
            CHECK(memcmp(coded, clean, n) == 0);        /* decoder repaired the codeword */
        }
    }
}

TEST(secded_failure_is_reported) {
    fec_config_t c = fec_config(CODE_SECDED84);
    uint8_t pay[K] = {0}, coded[FEC_MAX_CODED_BITS], out[K];
    size_t n = fec_encode(&c, pay, K, coded);
    coded[8] ^= 1; coded[9] ^= 1;                        /* two errors in block 1 */
    fec_result_t res;
    fec_decode(&c, coded, n, out, K, &res);
    CHECK_EQ_INT(res.failed_blocks, 1);
    CHECK_EQ_INT(res.blocks, 32);
}

int main(void) {
    RUN(names_roundtrip);
    RUN(coded_sizes);
    RUN(all_codes_roundtrip_clean);
    RUN(all_codes_fix_one_error_per_block);
    RUN(secded_failure_is_reported);
    return TEST_REPORT();
}
