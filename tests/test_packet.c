/* Failure-injection tests for the wire formats (packet.h, feedback.h) and the header-corruption
 * helper used by `channel --corrupt-header P`. A receiver must reject every malformed buffer
 * with -1 (never read past the end, never accept a wrong length). */
#include <string.h>
#include "testlib.h"
#include "fec.h"
#include "feedback.h"
#include "frame.h"
#include "packet.h"
#include "receiver.h"
#include "rng.h"

static int popcount8(uint8_t x) {
    int c = 0;
    for (; x; x &= (uint8_t)(x - 1)) c++;
    return c;
}

/* A valid DATA packet (hamming74, with ground-truth sidecar) serialized into wire. */
static size_t make_packet(uint8_t *wire, size_t cap, int with_truth) {
    static packet_t p;
    uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
    rng_t r;
    rng_seed(&r, 7);
    tag_random(&r, id);
    frame_build_payload(id, payload);
    fec_config_t fc = fec_config(CODE_HAMMING74);
    memset(&p, 0, sizeof p);
    p.type = PKT_DATA;
    p.code = (uint8_t)fc.code;
    p.rs_t = (uint8_t)fc.rs_t;
    p.interleave = 1;
    p.seq = 17;
    p.nbits = (uint16_t)fec_encode(&fc, payload, PAYLOAD_BITS, p.bits);
    if (with_truth) {
        p.has_truth = 1;
        memcpy(p.truth.tag, id, TAG_ID_BYTES);
        memcpy(p.truth.clean_bits, p.bits, p.nbits);
    }
    return packet_serialize(&p, wire, cap);
}

TEST(every_truncation_is_rejected) {
    static uint8_t wire[PACKET_MAX_BYTES];
    static packet_t q;
    for (int truth = 0; truth <= 1; truth++) {
        size_t len = make_packet(wire, sizeof wire, truth);
        CHECK(len > PACKET_HEADER_BYTES);
        CHECK(packet_parse(wire, len, &q) == 0);
        int rejected = 0;
        for (size_t l = 0; l < len; l++) rejected += packet_parse(wire, l, &q) != 0;
        CHECK_EQ_INT(rejected, (long long)len);
        CHECK(packet_parse(wire, len + 1, &q) != 0);          /* trailing garbage byte */
    }
}

TEST(bad_magic_version_type_rejected) {
    static uint8_t wire[PACKET_MAX_BYTES];
    static packet_t q;
    size_t len = make_packet(wire, sizeof wire, 1);
    for (int b = 0; b < 4; b++) {                             /* magic F, L, version, type */
        for (int v = 0; v < 256; v++) {
            uint8_t save = wire[b];
            wire[b] = (uint8_t)v;
            int ok = packet_parse(wire, len, &q) == 0;
            int legal = v == save || (b == 3 && (v == PKT_DATA || v == PKT_END));
            CHECK(ok == legal);
            wire[b] = save;
        }
    }
}

TEST(absurd_nbits_rejected_without_overflow) {
    static uint8_t big[PACKET_MAX_BYTES + 64];
    static packet_t q;
    memset(big, 0, sizeof big);
    size_t len = make_packet(big, sizeof big, 0);
    uint16_t bad[] = {0xFFFF, PACKET_MAX_BITS + 1, 0x8000};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        big[12] = (uint8_t)(bad[i] >> 8);
        big[13] = (uint8_t)bad[i];
        CHECK(packet_parse(big, len, &q) != 0);
        CHECK(packet_parse(big, sizeof big, &q) != 0);
    }
    /* nbits = 0 with an exact 16-byte packet parses, but no code produces 0 coded bits */
    big[12] = big[13] = 0;
    CHECK(packet_parse(big, PACKET_HEADER_BYTES, &q) == 0);
    fec_config_t cfg;
    CHECK(rx_config(q.code, q.rs_t, q.interleave, q.nbits, &cfg) != 0);
}

TEST(unknown_code_and_bad_parameters_rejected_by_receiver) {
    fec_config_t cfg;
    int accepted = 0;
    for (int code = 0; code < 256; code++)
        accepted += rx_config((uint8_t)code, 4, 1, 224, &cfg) == 0;
    /* 224 coded bits: hamming74 (32 x 7) and RS t=6 would need rs_t 6 - with rs_t 4 only hamming74 */
    CHECK_EQ_INT(accepted, 1);
    CHECK(rx_config(CODE_RS, 0, 1, 128, &cfg) != 0);           /* rs_t 0 */
    CHECK(rx_config(CODE_RS, 200, 1, 128 + 16 * 200, &cfg) != 0);
    CHECK(rx_config(CODE_HAMMING74, 4, 0, 224, &cfg) != 0);   /* interleaver depth 0 */
    CHECK(rx_config(CODE_HAMMING74, 4, 1, 225, &cfg) != 0);   /* size mismatch */
}

/* packet_parse itself refuses a DATA header with an unknown code, bad rs_t / interleave or unknown strategy. */
TEST(parse_rejects_invalid_code_parameters_and_strategy) {
    static uint8_t wire[PACKET_MAX_BYTES];
    static packet_t q;
    size_t len = make_packet(wire, sizeof wire, 0);
    CHECK(packet_parse(wire, len, &q) == 0);
    uint8_t save[4] = {wire[4], wire[5], wire[6], wire[15]};
    wire[4] = 99;                                    /* unknown code */
    CHECK(packet_parse(wire, len, &q) != 0);
    wire[4] = save[0];
    wire[6] = 0;                                     /* interleave depth 0 */
    CHECK(packet_parse(wire, len, &q) != 0);
    wire[6] = save[2];
    wire[15] = 4;                                    /* strategy past adaptive */
    CHECK(packet_parse(wire, len, &q) != 0);
    wire[15] = save[3];
    wire[4] = CODE_RS; wire[5] = 0;                  /* RS with rs_t 0 */
    CHECK(packet_parse(wire, len, &q) != 0);
    wire[4] = save[0]; wire[5] = 200;                /* rs_t is ignored by hamming: still fine */
    CHECK(packet_parse(wire, len, &q) == 0);
}

TEST(header_corruption_flips_only_header_bits) {
    static uint8_t wire[PACKET_MAX_BYTES], orig[PACKET_MAX_BYTES];
    size_t len = make_packet(wire, sizeof wire, 1);
    memcpy(orig, wire, len);
    rng_t r;
    rng_seed(&r, 99);
    int hist[PACKET_HEADER_CORRUPT_MAX + 2] = {0};
    for (int trial = 0; trial < 4000; trial++) {
        memcpy(wire, orig, len);
        int k = packet_corrupt_header(wire, len, &r);
        CHECK(k >= 1 && k <= PACKET_HEADER_CORRUPT_MAX);
        if (k < 1 || k > PACKET_HEADER_CORRUPT_MAX) continue;
        hist[k]++;
        int diff = 0;
        for (size_t i = 0; i < PACKET_HEADER_BYTES; i++) diff += popcount8((uint8_t)(wire[i] ^ orig[i]));
        CHECK_EQ_INT(diff, k);                                 /* k DISTINCT bits flipped */
        CHECK(memcmp(wire + PACKET_HEADER_BYTES, orig + PACKET_HEADER_BYTES,
                     len - PACKET_HEADER_BYTES) == 0);         /* payload + sidecar untouched */
    }
    for (int k = 1; k <= PACKET_HEADER_CORRUPT_MAX; k++)       /* uniform 1..MAX, ~1000 each */
        CHECK(hist[k] > 800 && hist[k] < 1200);
    CHECK(packet_corrupt_header(wire, PACKET_HEADER_BYTES - 1, &r) == 0); /* too short: no-op */
}

TEST(header_corruption_is_reproducible) {
    static uint8_t a[PACKET_MAX_BYTES], b[PACKET_MAX_BYTES];
    size_t len = make_packet(a, sizeof a, 0);
    memcpy(b, a, len);
    rng_t r1, r2;
    rng_seed(&r1, 5);
    rng_seed(&r2, 5);
    for (int i = 0; i < 100; i++) {
        packet_corrupt_header(a, len, &r1);
        packet_corrupt_header(b, len, &r2);
    }
    CHECK(memcmp(a, b, len) == 0);
}

TEST(every_single_header_bit_flip_is_safe) {
    /* Each of the 128 single-bit header errors either makes the packet unparsable, or parses to
     * a header the receiver rejects, or parses to SOME header - but never crashes or overreads. */
    static uint8_t wire[PACKET_MAX_BYTES];
    static packet_t q;
    size_t len = make_packet(wire, sizeof wire, 1);
    int parsed = 0, decodable = 0;
    for (int bit = 0; bit < 8 * PACKET_HEADER_BYTES; bit++) {
        wire[bit / 8] ^= (uint8_t)(0x80 >> (bit % 8));
        if (packet_parse(wire, len, &q) == 0) {
            parsed++;
            rx_decode_t d;
            if (rx_decode_packet(&q, &d) == 0) decodable++;
        }
        wire[bit / 8] ^= (uint8_t)(0x80 >> (bit % 8));
    }
    /* magic/version/type(most)/flags bit0/nbits flips are rejected; seq, attempt, strategy,
     * rs_t (ignored by hamming), flags bits 1-7 and interleave values still parse */
    CHECK(parsed > 0 && parsed < 8 * PACKET_HEADER_BYTES);
    CHECK(decodable <= parsed);
}

TEST(feedback_rejects_every_malformed_buffer) {
    feedback_t f, g;
    memset(&f, 0, sizeof f);
    f.type = FB_ACK;
    f.seq = 3;
    uint8_t wire[64];
    CHECK_EQ_INT(feedback_serialize(&f, wire, sizeof wire), FEEDBACK_BYTES);
    CHECK(feedback_serialize(&f, wire, FEEDBACK_BYTES - 1) == 0);
    for (size_t l = 0; l < sizeof wire; l++)
        CHECK((feedback_parse(wire, l, &g) == 0) == (l == FEEDBACK_BYTES));
    for (int b = 0; b < 4; b++) {
        for (int v = 0; v < 256; v++) {
            uint8_t save = wire[b];
            wire[b] = (uint8_t)v;
            int legal = v == save || (b == 3 && (v == FB_ACK || v == FB_NAK));
            CHECK((feedback_parse(wire, FEEDBACK_BYTES, &g) == 0) == legal);
            wire[b] = save;
        }
    }
}

int main(void) {
    printf("test_packet\n");
    RUN(every_truncation_is_rejected);
    RUN(bad_magic_version_type_rejected);
    RUN(absurd_nbits_rejected_without_overflow);
    RUN(unknown_code_and_bad_parameters_rejected_by_receiver);
    RUN(parse_rejects_invalid_code_parameters_and_strategy);
    RUN(header_corruption_flips_only_header_bits);
    RUN(header_corruption_is_reproducible);
    RUN(every_single_header_bit_flip_is_safe);
    RUN(feedback_rejects_every_malformed_buffer);
    return TEST_REPORT();
}
