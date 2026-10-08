/*
 * fec.h - frame-level forward error correction.
 *
 * A payload of K bits (128 here: 96-bit tag ID + CRC-32) is cut into consecutive chunks of k
 * bits (the last chunk zero-padded) and every chunk is encoded with an (n, k) block code:
 *
 *    payload  |---k---|---k---| ... |--k--000|          nblocks = ceil(K / k)
 *    coded    |----n----|----n----| ... |----n----|     coded bits = nblocks * n
 *
 * Effective code rate = K / coded bits (includes the padding overhead).
 * The decoder repairs the coded vector IN PLACE, so after fec_decode() the caller can compare
 * received vs. repaired bits to see exactly which bits the decoder changed.
 */
#ifndef FEC_H
#define FEC_H

#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_BITS_DEFAULT 128
#define FEC_MAX_CODED_BITS 2048

typedef enum {
    CODE_NONE = 0,       /* uncoded baseline */
    CODE_HAMMING74 = 1,
    CODE_HAMMING1511 = 2,
    CODE_SECDED84 = 3,
    CODE_BCH157 = 4,
    CODE_BCH3116 = 5,
    CODE_RS = 6
} code_id_t;

typedef struct {
    code_id_t id;
    const char *name;   /* CLI name, e.g. "hamming74" */
    const char *label;  /* pretty name, e.g. "Hamming(7,4)" */
    int n, k, t;        /* block length, data bits per block, guaranteed correctable bit errors */
} fec_code_info_t;

typedef struct {
    code_id_t code;
    int rs_t;           /* Reed-Solomon: correctable symbol errors (phase 6) */
    int interleave;     /* block-interleaver depth, 1 = off (phase 6) */
} fec_config_t;

typedef struct {
    int corrected_bits; /* bits the decoder changed */
    int failed_blocks;  /* blocks where the decoder DETECTED an uncorrectable pattern */
    int blocks;
} fec_result_t;

/* Registry of implemented codes. */
int fec_code_count(void);
const fec_code_info_t *fec_code_at(int index);
const fec_code_info_t *fec_code_info(code_id_t id);      /* NULL if not implemented */
int fec_code_from_name(const char *name, code_id_t *out); /* 0 ok, -1 unknown */
const char *fec_code_name(code_id_t id);

fec_config_t fec_config(code_id_t id);  /* default parameters for a code */
int fec_config_valid(const fec_config_t *cfg);
int fec_block_n(const fec_config_t *cfg);
int fec_block_k(const fec_config_t *cfg);
size_t fec_coded_bits(const fec_config_t *cfg, size_t kbits);
double fec_code_rate(const fec_config_t *cfg, size_t kbits);
void fec_describe(const fec_config_t *cfg, char *out, size_t outlen);

/* payload (kbits) -> coded; returns the number of coded bits. */
size_t fec_encode(const fec_config_t *cfg, const uint8_t *payload, size_t kbits, uint8_t *coded);

/* coded (nbits, repaired in place) -> payload (kbits). */
void fec_decode(const fec_config_t *cfg, uint8_t *coded, size_t nbits,
                uint8_t *payload, size_t kbits, fec_result_t *res);

#endif /* FEC_H */
