/* Seeded fuzz test for everything that parses bytes from the network or the command line.
 * Meant to run under `make sanitize` (AddressSanitizer + UndefinedBehaviorSanitizer), where any
 * out-of-bounds read, overflow or UB aborts the test; without sanitizers it still checks the
 * properties below. Fully reproducible (seeded RNG).
 *
 *  packet_parse   200k buffers: 100k random bytes of random length, 100k mutated valid packets
 *                 (byte flips, bit flips, truncation, extension). Property: a buffer that parses
 *                 re-serializes to a buffer that parses to the same packet; a parsed DATA packet
 *                 whose header is valid is decoded + classified by the real receive path.
 *  feedback_parse 200k buffers, same mix. Property: parse -> serialize gives the same bytes.
 *  schedule_parse 50k random strings over the spec alphabet. Property: no crash, sane result. */
#include <string.h>
#include "testlib.h"
#include "fec.h"
#include "feedback.h"
#include "frame.h"
#include "packet.h"
#include "receiver.h"
#include "rng.h"
#include "schedule.h"

#define N_FUZZ 100000

static size_t valid_packet(rng_t *r, uint8_t *wire, size_t cap) {
    static packet_t p;
    static const code_id_t CODES[] = {CODE_NONE, CODE_HAMMING74, CODE_HAMMING1511, CODE_SECDED84,
                                      CODE_BCH157, CODE_BCH3116, CODE_RS};
    uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
    tag_random(r, id);
    frame_build_payload(id, payload);
    fec_config_t fc = fec_config(CODES[rng_below(r, 7)]);
    fc.rs_t = 1 + (int)rng_below(r, 16);
    fc.interleave = 1 + (int)rng_below(r, 8);
    memset(&p, 0, sizeof p);
    p.type = rng_below(r, 10) ? PKT_DATA : PKT_END;
    p.code = (uint8_t)fc.code;
    p.rs_t = (uint8_t)fc.rs_t;
    p.interleave = (uint8_t)fc.interleave;
    p.seq = (uint32_t)rng_next(r);
    p.attempt = (uint8_t)rng_below(r, 5);
    p.strategy = (uint8_t)rng_below(r, 4);
    p.nbits = (uint16_t)fec_encode(&fc, payload, PAYLOAD_BITS, p.bits);
    p.has_truth = (int)rng_below(r, 2);
    if (p.has_truth) {
        memcpy(p.truth.tag, id, TAG_ID_BYTES);
        memcpy(p.truth.clean_bits, p.bits, p.nbits);
        p.truth.flips = (uint16_t)rng_below(r, 50);
    }
    return packet_serialize(&p, wire, cap);
}

/* Random edits: flip bytes/bits, truncate or extend with random bytes. Returns the new length. */
static size_t mutate(rng_t *r, uint8_t *buf, size_t len, size_t cap) {
    int edits = 1 + (int)rng_below(r, 6);
    for (int e = 0; e < edits; e++) {
        switch (rng_below(r, 5)) {
        case 0: if (len) buf[rng_below(r, (uint32_t)len)] = (uint8_t)rng_below(r, 256); break;
        case 1: if (len) buf[rng_below(r, (uint32_t)len)] ^= (uint8_t)(1u << rng_below(r, 8)); break;
        case 2: if (len) buf[rng_below(r, len < 16 ? (uint32_t)len : 16u)] ^= 0xFF; break; /* header */
        case 3: len = rng_below(r, (uint32_t)len + 1); break;                              /* truncate */
        default: {                                                                          /* extend */
            size_t add = rng_below(r, 40);
            for (size_t i = 0; i < add && len < cap; i++) buf[len++] = (uint8_t)rng_below(r, 256);
        }
        }
    }
    return len;
}

static int same_packet(const packet_t *a, const packet_t *b) {
    if (a->type != b->type || a->code != b->code || a->rs_t != b->rs_t ||
        a->interleave != b->interleave || a->seq != b->seq || a->nbits != b->nbits ||
        a->attempt != b->attempt || a->strategy != b->strategy || a->has_truth != b->has_truth)
        return 0;
    if (memcmp(a->bits, b->bits, a->nbits) != 0) return 0;
    if (!a->has_truth) return 1;
    return a->truth.flips == b->truth.flips && a->truth.profile == b->truth.profile &&
           a->truth.model == b->truth.model && !memcmp(a->truth.tag, b->truth.tag, TAG_ID_BYTES) &&
           !memcmp(a->truth.clean_bits, b->truth.clean_bits, a->nbits);
}

static long g_parsed, g_decoded;

static void check_packet_buffer(const uint8_t *buf, size_t len) {
    static packet_t p, q;
    static uint8_t again[PACKET_MAX_BYTES];
    memset(&p, 0, sizeof p);
    if (packet_parse(buf, len, &p) != 0) return;
    g_parsed++;
    size_t len2 = packet_serialize(&p, again, sizeof again);
    CHECK(len2 == len);                                   /* length is fully determined */
    memset(&q, 0, sizeof q);
    CHECK(packet_parse(again, len2, &q) == 0 && same_packet(&p, &q));
    if (p.type != PKT_DATA) return;
    rx_decode_t d;
    if (rx_decode_packet(&p, &d) != 0) return;            /* invalid header: receiver drops it */
    g_decoded++;
    frame_class_t c = rx_classify(&d, &p);
    CHECK(c >= CLASS_CORRECT && c <= CLASS_UNVERIFIED);
    CHECK(d.corrected_bits >= 0 && d.corrected_bits <= (int)p.nbits);
}

TEST(fuzz_packet_parse) {
    static uint8_t buf[PACKET_MAX_BYTES + 64];
    rng_t r;
    rng_seed(&r, 20261015);
    for (int i = 0; i < N_FUZZ; i++) {                    /* pure random bytes */
        size_t len = rng_below(&r, sizeof buf + 1);
        for (size_t j = 0; j < len; j++) buf[j] = (uint8_t)rng_below(&r, 256);
        if (len >= 3 && rng_below(&r, 2)) { buf[0] = 'F'; buf[1] = 'L'; buf[2] = PACKET_VERSION; }
        check_packet_buffer(buf, len);
    }
    for (int i = 0; i < N_FUZZ; i++) {                    /* mutated valid packets */
        size_t len = valid_packet(&r, buf, sizeof buf);
        len = mutate(&r, buf, len, sizeof buf);
        check_packet_buffer(buf, len);
    }
    printf("    packet_parse: %d buffers, %ld parsed, %ld decoded by the receiver\n",
           2 * N_FUZZ, g_parsed, g_decoded);
    CHECK(g_parsed > 1000 && g_decoded > 1000);           /* the fuzzer really reaches the decoders */
}

TEST(fuzz_feedback_parse) {
    uint8_t buf[FEEDBACK_BYTES + 32], again[FEEDBACK_BYTES];
    rng_t r;
    rng_seed(&r, 9002);
    long parsed = 0;
    for (int i = 0; i < 2 * N_FUZZ; i++) {
        size_t len;
        if (i < N_FUZZ) {
            len = rng_below(&r, sizeof buf + 1);
            for (size_t j = 0; j < len; j++) buf[j] = (uint8_t)rng_below(&r, 256);
            if (len >= 3 && rng_below(&r, 2)) { buf[0] = 'F'; buf[1] = 'B'; buf[2] = 1; }
        } else {
            feedback_t f;
            memset(&f, 0, sizeof f);
            f.type = rng_below(&r, 2) ? FB_ACK : FB_NAK;
            f.seq = (uint32_t)rng_next(&r);
            f.attempt = (uint8_t)rng_below(&r, 256);
            f.level = (uint8_t)rng_below(&r, 256);
            f.win_n = (uint16_t)rng_below(&r, 65536);
            f.win_fail = (uint16_t)rng_below(&r, 65536);
            f.win_corrected = (uint32_t)rng_next(&r);
            f.win_bits = (uint32_t)rng_next(&r);
            len = feedback_serialize(&f, buf, sizeof buf);
            len = mutate(&r, buf, len, sizeof buf);
        }
        feedback_t g;
        if (feedback_parse(buf, len, &g) != 0) continue;
        parsed++;
        CHECK(len == FEEDBACK_BYTES);
        CHECK(feedback_serialize(&g, again, sizeof again) == FEEDBACK_BYTES);
        CHECK(memcmp(again, buf, FEEDBACK_BYTES) == 0);   /* exact round trip */
    }
    printf("    feedback_parse: %d buffers, %ld parsed\n", 2 * N_FUZZ, parsed);
    CHECK(parsed > 1000);
}

TEST(fuzz_schedule_parse) {
    static const char ALPHA[] = "tollhospital:,0123456789-+ x";
    char spec[64];
    rng_t r;
    rng_seed(&r, 31);
    long ok = 0;
    for (int i = 0; i < N_FUZZ / 2; i++) {
        size_t len = rng_below(&r, sizeof spec);
        for (size_t j = 0; j < len; j++) spec[j] = ALPHA[rng_below(&r, sizeof ALPHA - 1)];
        spec[len] = '\0';
        if (rng_below(&r, 3) == 0) {                      /* make some of them almost valid */
            snprintf(spec, sizeof spec, "%s:%u,%s:%d", rng_below(&r, 2) ? "toll" : "hospital",
                     (unsigned)rng_below(&r, 100000), rng_below(&r, 2) ? "toll" : "hosp",
                     (int)rng_below(&r, 2000) - 1000);
        }
        schedule_t s;
        if (schedule_parse(spec, &s) != 0) continue;
        ok++;
        CHECK(s.n >= 1 && s.n <= SCHEDULE_MAX);
        CHECK(schedule_segment_at(&s, 0) == 0);
        int last = schedule_segment_at(&s, 2000000000L);
        CHECK(last >= 0 && last < s.n);
    }
    printf("    schedule_parse: %d strings, %ld accepted\n", N_FUZZ / 2, ok);
}

int main(void) {
    printf("test_fuzz\n");
    RUN(fuzz_packet_parse);
    RUN(fuzz_feedback_parse);
    RUN(fuzz_schedule_parse);
    return TEST_REPORT();
}
