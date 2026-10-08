#include "secded.h"
#include "hamming.h"

static uint8_t parity(const uint8_t *bits, int n) {
    uint8_t p = 0;
    for (int i = 0; i < n; i++) p ^= bits[i];
    return p;
}

void secded84_encode(const uint8_t data[4], uint8_t code[8]) {
    hamming_encode(3, data, code);
    code[7] = parity(code, 7); /* makes the XOR of all 8 bits zero */
}

int secded84_decode(uint8_t code[8], uint8_t data[4]) {
    int s = hamming_syndrome(3, code);
    uint8_t p = parity(code, 8);
    int status;
    if (s == 0 && p == 0) {
        status = SECDED_OK;
    } else if (p == 1) {
        if (s != 0) code[s - 1] ^= 1; /* single error inside the Hamming part */
        else        code[7] ^= 1;     /* single error on the overall parity bit */
        status = SECDED_CORRECTED;
    } else {
        status = SECDED_DOUBLE;       /* s != 0 but parity even: two errors */
    }
    /* data positions of Hamming(7,4): 3, 5, 6, 7 (1-indexed) */
    data[0] = code[2]; data[1] = code[4]; data[2] = code[5]; data[3] = code[6];
    return status;
}
