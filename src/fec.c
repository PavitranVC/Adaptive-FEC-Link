#include <stdio.h>
#include <string.h>
#include "bch.h"
#include "bits.h"
#include "fec.h"
#include "hamming.h"
#include "interleave.h"
#include "rs.h"
#include "secded.h"

/* ---- adapters: every block code gets the same signature ----
 * encode: k data bits -> n code bits
 * decode: repairs n code bits in place, writes k data bits,
 *         returns #bits corrected (>= 0) or < 0 if an uncorrectable error was detected.
 * The config is passed along for codes with parameters (Reed-Solomon's t). */
typedef void (*block_enc_fn)(const fec_config_t *cfg, const uint8_t *data, uint8_t *code);
typedef int (*block_dec_fn)(const fec_config_t *cfg, uint8_t *code, uint8_t *data);

#define UNUSED(x) (void)(x)

static void none_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) { UNUSED(f); c[0] = d[0]; }
static int none_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) { UNUSED(f); d[0] = c[0]; return 0; }
static void h74_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) { UNUSED(f); hamming_encode(3, d, c); }
static int h74_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) { UNUSED(f); return hamming_decode(3, c, d); }
static void h1511_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) { UNUSED(f); hamming_encode(4, d, c); }
static int h1511_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) { UNUSED(f); return hamming_decode(4, c, d); }
static void sd84_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) { UNUSED(f); secded84_encode(d, c); }
static int sd84_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) {
    UNUSED(f);
    int s = secded84_decode(c, d);
    return s == SECDED_DOUBLE ? -1 : s;
}
static void bch157_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) { UNUSED(f); bch_encode(bch157(), d, c); }
static int bch157_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) { UNUSED(f); return bch_decode(bch157(), c, d); }
static void bch3116_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) { UNUSED(f); bch_encode(bch3116(), d, c); }
static int bch3116_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) { UNUSED(f); return bch_decode(bch3116(), c, d); }

/* Reed-Solomon works on bytes: the whole 128-bit payload is ONE codeword of 16 data bytes. */
#define RS_DATA_BYTES (PAYLOAD_BITS_DEFAULT / 8)
static void rs_enc(const fec_config_t *f, const uint8_t *d, uint8_t *c) {
    const rs_code_t *rs = rs_get(RS_DATA_BYTES, f->rs_t);
    uint8_t data[RS_DATA_BYTES], cw[255];
    bits_pack(d, PAYLOAD_BITS_DEFAULT, data);
    rs_encode(rs, data, cw);
    bits_unpack(cw, (size_t)rs->n * 8, c);
}
static int rs_dec(const fec_config_t *f, uint8_t *c, uint8_t *d) {
    const rs_code_t *rs = rs_get(RS_DATA_BYTES, f->rs_t);
    uint8_t data[RS_DATA_BYTES], cw[255];
    bits_pack(c, (size_t)rs->n * 8, cw);
    int ret = rs_decode(rs, cw, data);
    bits_unpack(cw, (size_t)rs->n * 8, c);
    bits_unpack(data, PAYLOAD_BITS_DEFAULT, d);
    return ret;
}

typedef struct {
    fec_code_info_t info;
    block_enc_fn enc;
    block_dec_fn dec;
} code_entry_t;

/* For RS, n/k/t below are the defaults (t = 4 bytes); the real n depends on --rs-t. */
static const code_entry_t CODES[] = {
    {{CODE_NONE, "none", "uncoded", 1, 1, 0}, none_enc, none_dec},
    {{CODE_HAMMING74, "hamming74", "Hamming(7,4)", 7, 4, 1}, h74_enc, h74_dec},
    {{CODE_HAMMING1511, "hamming1511", "Hamming(15,11)", 15, 11, 1}, h1511_enc, h1511_dec},
    {{CODE_SECDED84, "secded84", "SECDED(8,4)", 8, 4, 1}, sd84_enc, sd84_dec},
    {{CODE_BCH157, "bch157", "BCH(15,7)", 15, 7, 2}, bch157_enc, bch157_dec},
    {{CODE_BCH3116, "bch3116", "BCH(31,16)", 31, 16, 3}, bch3116_enc, bch3116_dec},
    {{CODE_RS, "rs", "RS(24,16) over GF(256)", 192, 128, 4}, rs_enc, rs_dec},
};

#define NCODES ((int)(sizeof CODES / sizeof CODES[0]))

static const code_entry_t *entry(code_id_t id) {
    for (int i = 0; i < NCODES; i++)
        if (CODES[i].info.id == id) return &CODES[i];
    return NULL;
}

int fec_code_count(void) { return NCODES; }
const fec_code_info_t *fec_code_at(int i) { return (i >= 0 && i < NCODES) ? &CODES[i].info : NULL; }

const fec_code_info_t *fec_code_info(code_id_t id) {
    const code_entry_t *e = entry(id);
    return e ? &e->info : NULL;
}

int fec_code_from_name(const char *name, code_id_t *out) {
    for (int i = 0; i < NCODES; i++)
        if (strcmp(name, CODES[i].info.name) == 0) { *out = CODES[i].info.id; return 0; }
    return -1;
}

const char *fec_code_name(code_id_t id) {
    const code_entry_t *e = entry(id);
    return e ? e->info.name : "?";
}

fec_config_t fec_config(code_id_t id) {
    fec_config_t c;
    c.code = id;
    c.rs_t = 4;
    c.interleave = 1;
    return c;
}

int fec_config_valid(const fec_config_t *cfg) {
    if (entry(cfg->code) == NULL || cfg->interleave < 1) return 0;
    return cfg->code != CODE_RS || rs_get(RS_DATA_BYTES, cfg->rs_t) != NULL;
}

int fec_block_n(const fec_config_t *cfg) {
    if (cfg->code == CODE_RS) return 8 * (RS_DATA_BYTES + 2 * cfg->rs_t);
    return entry(cfg->code)->info.n;
}

int fec_block_k(const fec_config_t *cfg) { return entry(cfg->code)->info.k; }

static size_t nblocks(const fec_config_t *cfg, size_t kbits) {
    size_t k = (size_t)fec_block_k(cfg);
    return (kbits + k - 1) / k;
}

size_t fec_coded_bits(const fec_config_t *cfg, size_t kbits) {
    return nblocks(cfg, kbits) * (size_t)fec_block_n(cfg);
}

double fec_code_rate(const fec_config_t *cfg, size_t kbits) {
    return (double)kbits / (double)fec_coded_bits(cfg, kbits);
}

void fec_describe(const fec_config_t *cfg, char *out, size_t outlen) {
    const fec_code_info_t *i = fec_code_info(cfg->code);
    size_t nb = fec_coded_bits(cfg, PAYLOAD_BITS_DEFAULT);
    char il[32] = "";
    if (cfg->interleave > 1) snprintf(il, sizeof il, ", interleaver depth %d", cfg->interleave);
    if (cfg->code == CODE_RS)
        snprintf(out, outlen, "RS(%d,%d) over GF(256) t=%d bytes, %zu coded bits/frame, rate %.3f%s",
                 RS_DATA_BYTES + 2 * cfg->rs_t, RS_DATA_BYTES, cfg->rs_t, nb,
                 fec_code_rate(cfg, PAYLOAD_BITS_DEFAULT), il);
    else
        snprintf(out, outlen, "%s n=%d k=%d t=%d, %zu coded bits/frame, rate %.3f%s",
                 i->label, i->n, i->k, i->t, nb, fec_code_rate(cfg, PAYLOAD_BITS_DEFAULT), il);
}

size_t fec_encode(const fec_config_t *cfg, const uint8_t *payload, size_t kbits, uint8_t *coded) {
    const code_entry_t *e = entry(cfg->code);
    size_t n = (size_t)fec_block_n(cfg), k = (size_t)fec_block_k(cfg), nb = nblocks(cfg, kbits);
    uint8_t chunk[PAYLOAD_BITS_DEFAULT], work[FEC_MAX_CODED_BITS];
    for (size_t b = 0; b < nb; b++) {
        for (size_t j = 0; j < k; j++) {           /* copy chunk, zero-pad past the payload */
            size_t i = b * k + j;
            chunk[j] = i < kbits ? payload[i] : 0;
        }
        e->enc(cfg, chunk, work + b * n);
    }
    interleave(work, coded, nb * n, cfg->interleave);  /* depth 1 = plain copy */
    return nb * n;
}

void fec_decode(const fec_config_t *cfg, uint8_t *coded, size_t nbits,
                uint8_t *payload, size_t kbits, fec_result_t *res) {
    const code_entry_t *e = entry(cfg->code);
    size_t n = (size_t)fec_block_n(cfg), k = (size_t)fec_block_k(cfg), nb = nbits / n;
    uint8_t chunk[PAYLOAD_BITS_DEFAULT], before[FEC_MAX_CODED_BITS], work[FEC_MAX_CODED_BITS];
    memset(res, 0, sizeof *res);
    res->blocks = (int)nb;
    deinterleave(coded, work, nbits, cfg->interleave);
    for (size_t b = 0; b < nb; b++) {
        uint8_t *blk = work + b * n;
        memcpy(before, blk, n);
        if (e->dec(cfg, blk, chunk) < 0) res->failed_blocks++;
        res->corrected_bits += (int)bits_distance(before, blk, n);
        for (size_t j = 0; j < k; j++) {
            size_t i = b * k + j;
            if (i < kbits) payload[i] = chunk[j];
        }
    }
    interleave(work, coded, nbits, cfg->interleave);   /* repaired word, transmission order */
}
