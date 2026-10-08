/*
 * vehicle - the sender (the car's RFID tag + transponder).
 *
 * For each frame: draw a random 96-bit tag ID (seeded), append CRC-32, FEC-encode the 128-bit
 * payload with --code, and send it over UDP to the channel (127.0.0.1:9000 by default).
 * After --count frames it sends an END packet carrying the number of frames sent.
 */
#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "fec.h"
#include "frame.h"
#include "netutil.h"
#include "packet.h"
#include "term.h"

#define TAG "VEHICLE"

static int send_packet(int fd, const cli_opts_t *o, const packet_t *p) {
    uint8_t wire[PACKET_MAX_BYTES];
    size_t len = packet_serialize(p, wire, sizeof wire);
    if (len == 0 || net_sendto(fd, o->host, o->channel_port, wire, len) != 0) {
        term_say(TAG, T_BLUE, "%serror:%s send failed", term_c(T_RED), term_c(T_RESET));
        return -1;
    }
    return 0;
}

static void send_end(int fd, const cli_opts_t *o, uint32_t sent) {
    packet_t p;
    memset(&p, 0, sizeof p);
    p.type = PKT_END;
    p.seq = sent;
    /* sent twice: UDP gives no delivery guarantee; receivers ignore the duplicate */
    send_packet(fd, o, &p);
    sleep_us(20000);
    send_packet(fd, o, &p);
}

int main(int argc, char **argv) {
    static packet_t pkt;
    cli_opts_t o;
    cli_defaults(&o);
    int rc = cli_parse(&o, argc, argv);
    if (rc) return rc > 0 ? 0 : 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    term_init(o.color);
    install_stop_handler();

    fec_config_t fc = cli_fec_config(&o);
    char desc[160];
    fec_describe(&fc, desc, sizeof desc);
    int fd = net_udp_socket();
    if (fd < 0) return 1;

    rng_t rng;
    rng_seed(&rng, rng_derive(o.seed, "vehicle"));
    term_say(TAG, T_BLUE, "sending %ld frames to channel %s:%d | code %s | seed %llu",
             o.count, o.host, o.channel_port, desc, (unsigned long long)o.seed);

    long sent = 0;
    for (long seq = 0; seq < o.count && !stop_requested(); seq++) {
        uint8_t id[TAG_ID_BYTES], payload[PAYLOAD_BITS];
        char hex[TAG_HEX_LEN + 1];
        tag_random(&rng, id);
        frame_build_payload(id, payload);

        memset(&pkt, 0, sizeof pkt);
        pkt.type = PKT_DATA;
        pkt.code = (uint8_t)fc.code;
        pkt.rs_t = (uint8_t)fc.rs_t;
        pkt.interleave = (uint8_t)fc.interleave;
        pkt.seq = (uint32_t)seq;
        pkt.nbits = (uint16_t)fec_encode(&fc, payload, PAYLOAD_BITS, pkt.bits);
        if (send_packet(fd, &o, &pkt)) break;
        sent++;

        if (!o.quiet) {
            tag_to_hex(id, hex);
            term_say(TAG, T_BLUE, "seq=%-4ld tag=%s -> %u coded bits", seq, hex, pkt.nbits);
        }
        /* --delay-ms paces the demo; without it a 100 us gap keeps loopback buffers happy */
        sleep_us(o.delay_ms > 0 ? (long)o.delay_ms * 1000L : 100L);
    }
    term_say(TAG, T_BLUE, "done: %ld frames sent", sent);
    send_end(fd, &o, (uint32_t)sent);
    net_close(fd);
    return 0;
}
