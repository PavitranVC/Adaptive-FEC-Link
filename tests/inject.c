/*
 * inject - failure-injection helper for tests/test_failure.sh (built as build/tests/inject).
 * Sends hand-made bad UDP packets to a receiver so the test can check it logs and drops them.
 *
 *   inject --port P malformed   garbage (incl. an EMPTY datagram), truncated, bad magic, bad
 *                               version, bad type, absurd nbits, unknown code id, then valid
 *                               DATA seq 0, an exact duplicate of it, seq 1, a late copy of
 *                               seq 0 - and NO END (the receiver must stop on its idle timeout)
 *   inject --port P end N       one END packet carrying "N frames sent"
 *   inject --port P fbgarbage   200 malformed feedback packets (for the vehicle's port 9002)
 *
 * Prints one line per category: "<name> <count>", so the test knows what to expect.
 *
 * test_failure.sh compares the receiver's log and summary counts against these numbers exactly,
 * so all random content is seeded: a changed seed or packet list changes what the test expects.
 * A scenario sending the wrong number of packets, or one that the receiver accepts as valid,
 * would show up there as a count mismatch, not here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fec.h"
#include "frame.h"
#include "netutil.h"
#include "packet.h"
#include "rng.h"

static int g_fd, g_port;

/* Sends one datagram; the pause keeps loopback ordering so the duplicate/late-copy cases are deterministic. */
static void send_raw(const uint8_t *buf, size_t len) {
    net_sendto(g_fd, "127.0.0.1", g_port, buf, len);
    sleep_us(2000);   /* keep the order on loopback and let the receiver log */
}

/* A valid DATA packet as the channel would forward it (hamming74, with ground truth). */
static size_t valid_packet(uint32_t seq, uint8_t *wire, size_t cap) {
    static packet_t p;
    uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
    rng_t r;
    rng_seed(&r, 1000 + seq);
    tag_random(&r, id);
    frame_build_payload(id, payload);
    fec_config_t fc = fec_config(CODE_HAMMING74);
    memset(&p, 0, sizeof p);
    p.type = PKT_DATA;
    p.code = (uint8_t)fc.code;
    p.rs_t = (uint8_t)fc.rs_t;
    p.interleave = 1;
    p.seq = seq;
    p.nbits = (uint16_t)fec_encode(&fc, payload, PAYLOAD_BITS, p.bits);
    p.has_truth = 1;
    memcpy(p.truth.tag, id, TAG_ID_BYTES);
    memcpy(p.truth.clean_bits, p.bits, p.nbits);
    return packet_serialize(&p, wire, cap);
}

/* Malformed-input paths of packet_parse and the receiver's code/size check, then valid traffic,
 * a duplicate and a stale copy to exercise de-duplication. Counts printed here are test_failure.sh's oracle. */
static void scenario_malformed(void) {
    static uint8_t wire[PACKET_MAX_BYTES], bad[PACKET_MAX_BYTES];
    rng_t r;
    rng_seed(&r, 4242);
    int garbage = 0, malformed = 0;
    send_raw(bad, 0);                                   /* empty datagram */
    garbage++;
    for (int i = 0; i < 30; i++) {                      /* random bytes, random length */
        size_t len = 1 + rng_below(&r, 300);
        for (size_t j = 0; j < len; j++) bad[j] = (uint8_t)rng_below(&r, 256);
        send_raw(bad, len);
        garbage++;
    }
    size_t len = valid_packet(0, wire, sizeof wire);
    send_raw(wire, len / 2);                            /* truncated */
    send_raw(wire, PACKET_HEADER_BYTES - 1);            /* shorter than a header */
    malformed += 2;
    const int fields[][2] = {{0, 'X'}, {2, 9}, {3, 7}}; /* bad magic, bad version, bad type */
    for (int i = 0; i < 3; i++) {
        memcpy(bad, wire, len);
        bad[fields[i][0]] = (uint8_t)fields[i][1];
        send_raw(bad, len);
        malformed++;
    }
    memcpy(bad, wire, len);                             /* absurd nbits = 65535 */
    bad[12] = bad[13] = 0xFF;
    send_raw(bad, len);
    malformed++;
    memcpy(bad, wire, len);                             /* unknown code id, length still valid */
    bad[4] = 99;
    send_raw(bad, len);
    malformed++;                                        /* packet_parse rejects the header itself */
    printf("garbage %d\nmalformed %d\ninvalid 0\n", garbage, malformed);

    send_raw(wire, len);                                /* seq 0 */
    send_raw(wire, len);                                /* exact duplicate of seq 0 */
    size_t len1 = valid_packet(1, bad, sizeof bad);
    send_raw(bad, len1);                                /* seq 1 */
    send_raw(wire, len);                                /* late copy of the old seq 0 */
    printf("valid 2\nduplicates 2\n");                  /* ... and no END: the END is "lost" */
}

/* Feedback parser robustness: random lengths 0..63, half with a correct "FB" prefix so they get
 * past the magic check. Fixed length 22 (FEEDBACK_BYTES) with bad version/type is not forced. */
static void scenario_fbgarbage(void) {
    uint8_t bad[64];
    rng_t r;
    rng_seed(&r, 77);
    for (int i = 0; i < 200; i++) {
        size_t len = rng_below(&r, sizeof bad);
        for (size_t j = 0; j < len; j++) bad[j] = (uint8_t)rng_below(&r, 256);
        if (len >= 2 && (i & 1)) { bad[0] = 'F'; bad[1] = 'B'; }   /* half look almost right */
        send_raw(bad, len);
    }
    printf("fbgarbage 200\n");
}

int main(int argc, char **argv) {
    if (argc < 4 || strcmp(argv[1], "--port") != 0) {
        fprintf(stderr, "usage: inject --port P {malformed | end N | fbgarbage}\n");
        return 2;
    }
    g_port = atoi(argv[2]);
    g_fd = net_udp_socket();
    if (g_fd < 0) return 1;
    if (!strcmp(argv[3], "malformed")) {
        scenario_malformed();
    } else if (!strcmp(argv[3], "end") && argc > 4) {
        packet_t p;
        uint8_t wire[PACKET_MAX_BYTES];
        memset(&p, 0, sizeof p);
        p.type = PKT_END;
        p.seq = (uint32_t)atol(argv[4]);
        send_raw(wire, packet_serialize(&p, wire, sizeof wire));
    } else if (!strcmp(argv[3], "fbgarbage")) {
        scenario_fbgarbage();
    } else {
        fprintf(stderr, "inject: unknown scenario '%s'\n", argv[3]);
        return 2;
    }
    net_close(g_fd);
    return 0;
}
