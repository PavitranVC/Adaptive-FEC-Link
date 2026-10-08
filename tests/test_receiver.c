/* Task 1 - integrity check for decision #6: the receiver's DECODER must never use the channel's
 * ground-truth sidecar. Decoding the same corrupted frames with the sidecar present, zeroed,
 * filled with garbage, or stripped must give bit-identical decoder outputs. Only the
 * classification (CORRECT / SILENT_WRONG vs UNVERIFIED) may depend on the sidecar. */
#include <string.h>
#include "testlib.h"
#include "fec.h"
#include "frame.h"
#include "noise.h"
#include "packet.h"
#include "receiver.h"

static packet_t clean, noisy, variant;

/* Build a channel-style packet: encoded tag, noise applied, truth sidecar filled in. */
static void make_packet(const fec_config_t *fc, rng_t *r, noise_t *ch) {
    uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
    tag_random(r, id);
    frame_build_payload(id, payload);
    memset(&noisy, 0, sizeof noisy);
    noisy.type = PKT_DATA;
    noisy.code = (uint8_t)fc->code;
    noisy.rs_t = (uint8_t)fc->rs_t;
    noisy.interleave = (uint8_t)fc->interleave;
    noisy.nbits = (uint16_t)fec_encode(fc, payload, PAYLOAD_BITS, noisy.bits);
    memcpy(noisy.truth.clean_bits, noisy.bits, noisy.nbits);
    memcpy(noisy.truth.tag, id, TAG_ID_BYTES);
    noisy.truth.flips = (uint16_t)noise_apply(ch, noisy.bits, noisy.nbits, NULL);
    noisy.has_truth = 1;
    clean = noisy;
}

static int same_decode(const rx_decode_t *a, const rx_decode_t *b, int nbits) {
    return a->crc_ok == b->crc_ok && a->failed_blocks == b->failed_blocks &&
           a->corrected_bits == b->corrected_bits &&
           memcmp(a->id, b->id, TAG_ID_BYTES) == 0 &&
           memcmp(a->repaired, b->repaired, (size_t)nbits) == 0;
}

TEST(decode_identical_with_truth_present_zeroed_garbage_or_stripped) {
    const code_id_t codes[] = {CODE_NONE, CODE_HAMMING74, CODE_HAMMING1511, CODE_SECDED84,
                               CODE_BCH157, CODE_BCH3116, CODE_RS};
    rng_t r;
    rng_seed(&r, 6);
    int mismatches = 0, frames = 0, with_errors = 0;
    for (size_t c = 0; c < sizeof codes / sizeof codes[0]; c++) {
        fec_config_t fc = fec_config(codes[c]);
        noise_params_t np = noise_ge(0.01, 0.1, 0.01, 0.5);   /* plenty of errors and failures */
        noise_t ch;
        noise_init(&ch, &np, 100 + c);
        for (int f = 0; f < 300; f++) {
            make_packet(&fc, &r, &ch);
            rx_decode_t ref, other;
            CHECK(rx_decode_packet(&clean, &ref) == 0);
            with_errors += clean.truth.flips > 0;

            variant = clean;                                   /* zeroed sidecar */
            memset(&variant.truth, 0, sizeof variant.truth);
            rx_decode_packet(&variant, &other);
            mismatches += !same_decode(&ref, &other, clean.nbits);

            variant = clean;                                   /* garbage sidecar */
            for (int i = 0; i < variant.nbits; i++) variant.truth.clean_bits[i] = (uint8_t)(rng_next(&r) & 1);
            for (int i = 0; i < TAG_ID_BYTES; i++) variant.truth.tag[i] = (uint8_t)rng_next(&r);
            variant.truth.flips = 999;
            rx_decode_packet(&variant, &other);
            mismatches += !same_decode(&ref, &other, clean.nbits);

            variant = clean;                                   /* stripped (no sidecar) */
            variant.has_truth = 0;
            rx_decode_packet(&variant, &other);
            mismatches += !same_decode(&ref, &other, clean.nbits);

            /* ...and through the wire format without the sidecar */
            uint8_t wire[PACKET_MAX_BYTES];
            size_t len = packet_serialize(&variant, wire, sizeof wire);
            packet_t parsed;
            CHECK(packet_parse(wire, len, &parsed) == 0 && parsed.has_truth == 0);
            rx_decode_packet(&parsed, &other);
            mismatches += !same_decode(&ref, &other, clean.nbits);
            frames++;
        }
    }
    CHECK_EQ_INT(mismatches, 0);
    CHECK(with_errors > frames / 2);   /* the test really exercised the decoders */
}

TEST(truth_only_changes_classification) {
    fec_config_t fc = fec_config(CODE_HAMMING74);
    noise_params_t np = noise_bsc(0.0);
    noise_t ch;
    rng_t r;
    rng_seed(&r, 7);
    noise_init(&ch, &np, 7);
    make_packet(&fc, &r, &ch);
    rx_decode_t d;
    rx_decode_packet(&clean, &d);
    CHECK(rx_classify(&d, &clean) == CLASS_CORRECT);
    variant = clean;
    variant.has_truth = 0;
    CHECK(rx_classify(&d, &variant) == CLASS_UNVERIFIED);
    variant = clean;
    variant.truth.tag[0] ^= 1;               /* a lying oracle can only change the verdict */
    CHECK(rx_classify(&d, &variant) == CLASS_SILENT_WRONG);
}

TEST(rejects_invalid_header) {
    rx_decode_t d;
    memset(&variant, 0, sizeof variant);
    variant.type = PKT_DATA;
    variant.code = 99;
    variant.nbits = 10;
    CHECK(rx_decode_packet(&variant, &d) != 0);
    variant.code = CODE_HAMMING74;
    variant.interleave = 1;
    variant.nbits = 100;                     /* wrong size for Hamming(7,4) */
    CHECK(rx_decode_packet(&variant, &d) != 0);
}

int main(void) {
    RUN(decode_identical_with_truth_present_zeroed_garbage_or_stripped);
    RUN(truth_only_changes_classification);
    RUN(rejects_invalid_header);
    return TEST_REPORT();
}
