/* Tests for tag/payload framing, classification and the UDP wire format. */
#include <string.h>
#include "testlib.h"
#include "bits.h"
#include "crc32.h"
#include "frame.h"
#include "packet.h"

static const uint8_t ID[TAG_ID_BYTES] = {0xE2, 0x80, 0x68, 0x94, 0x00, 0x00, 0x50, 0x1E,
                                         0x2A, 0x3B, 0x4C, 0x5D};

TEST(payload_is_id_then_crc) {
    uint8_t bits[PAYLOAD_BITS], bytes[16];
    frame_build_payload(ID, bits);
    bits_pack(bits, PAYLOAD_BITS, bytes);
    CHECK(memcmp(bytes, ID, 12) == 0);
    uint32_t crc = crc32_compute(ID, 12);
    CHECK_EQ_INT(bytes[12], (crc >> 24) & 0xFF);
    CHECK_EQ_INT(bytes[15], crc & 0xFF);
}

TEST(parse_checks_crc) {
    uint8_t bits[PAYLOAD_BITS], id[TAG_ID_BYTES];
    frame_build_payload(ID, bits);
    CHECK(frame_parse_payload(bits, id) == 1);
    CHECK(memcmp(id, ID, 12) == 0);
    bits[17] ^= 1;
    CHECK(frame_parse_payload(bits, id) == 0);
    bits[17] ^= 1;
    bits[120] ^= 1;                                     /* error in the CRC field itself */
    CHECK(frame_parse_payload(bits, id) == 0);
}

TEST(classification_rules) {
    uint8_t other[TAG_ID_BYTES];
    memcpy(other, ID, 12);
    other[3] ^= 0x10;
    CHECK(frame_classify(1, 0, ID, ID) == CLASS_CORRECT);
    CHECK(frame_classify(0, 0, ID, ID) == CLASS_DETECTED_FAIL);
    CHECK(frame_classify(1, 1, ID, ID) == CLASS_DETECTED_FAIL);  /* decoder flagged failure */
    CHECK(frame_classify(1, 0, other, ID) == CLASS_SILENT_WRONG);
    CHECK(frame_classify(1, 0, ID, NULL) == CLASS_UNVERIFIED);
    CHECK(strcmp(frame_class_name(CLASS_SILENT_WRONG), "SILENT_WRONG") == 0);
}

TEST(tag_hex) {
    char hex[TAG_HEX_LEN + 1];
    tag_to_hex(ID, hex);
    CHECK(strcmp(hex, "E28068940000501E2A3B4C5D") == 0);
}

TEST(packet_roundtrip_with_truth) {
    packet_t p, q;
    memset(&p, 0, sizeof p);
    p.type = PKT_DATA;
    p.code = CODE_SECDED84;
    p.rs_t = 4;
    p.interleave = 1;
    p.seq = 123456;
    p.nbits = 256;
    for (int i = 0; i < 256; i++) p.bits[i] = (uint8_t)((i * 7) % 3 == 0);
    p.has_truth = 1;
    p.truth.flips = 3;
    p.truth.profile = 1;
    p.truth.model = 2;
    for (int i = 0; i < 256; i++) p.truth.clean_bits[i] = (uint8_t)(i % 2);
    memcpy(p.truth.tag, ID, 12);

    uint8_t wire[PACKET_MAX_BYTES];
    size_t len = packet_serialize(&p, wire, sizeof wire);
    CHECK(len > 0);
    CHECK(packet_parse(wire, len, &q) == 0);
    CHECK_EQ_INT(q.type, PKT_DATA);
    CHECK_EQ_INT(q.code, CODE_SECDED84);
    CHECK_EQ_INT(q.rs_t, 4);
    CHECK_EQ_INT(q.seq, 123456);
    CHECK_EQ_INT(q.nbits, 256);
    CHECK(memcmp(p.bits, q.bits, 256) == 0);
    CHECK_EQ_INT(q.has_truth, 1);
    CHECK_EQ_INT(q.truth.flips, 3);
    CHECK_EQ_INT(q.truth.model, 2);
    CHECK(memcmp(p.truth.clean_bits, q.truth.clean_bits, 256) == 0);
    CHECK(memcmp(q.truth.tag, ID, 12) == 0);
}

TEST(packet_without_truth_and_end) {
    packet_t p, q;
    memset(&p, 0, sizeof p);
    p.type = PKT_END;
    p.seq = 20;           /* END carries the number of frames sent */
    uint8_t wire[PACKET_MAX_BYTES];
    size_t len = packet_serialize(&p, wire, sizeof wire);
    CHECK(packet_parse(wire, len, &q) == 0);
    CHECK_EQ_INT(q.type, PKT_END);
    CHECK_EQ_INT(q.seq, 20);
    CHECK_EQ_INT(q.has_truth, 0);
}

TEST(packet_rejects_garbage) {
    packet_t q;
    uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(packet_parse(junk, sizeof junk, &q) != 0);
    packet_t p;
    memset(&p, 0, sizeof p);
    p.type = PKT_DATA;
    p.nbits = 100;
    uint8_t wire[PACKET_MAX_BYTES];
    size_t len = packet_serialize(&p, wire, sizeof wire);
    CHECK(packet_parse(wire, len - 1, &q) != 0);       /* truncated */
    wire[12] = 0xFF; wire[13] = 0xFF;                   /* absurd nbits */
    CHECK(packet_parse(wire, len, &q) != 0);
}

int main(void) {
    RUN(payload_is_id_then_crc);
    RUN(parse_checks_crc);
    RUN(classification_rules);
    RUN(tag_hex);
    RUN(packet_roundtrip_with_truth);
    RUN(packet_without_truth_and_end);
    RUN(packet_rejects_garbage);
    return TEST_REPORT();
}
