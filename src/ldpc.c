#include <string.h>
#include "bits.h"
#include "ldpc.h"
#include "rng.h"

/* ------------------------------------------------------------------------------------------
 * Construction of H: a simplified "progressive edge growth" (PEG, Hu/Eleftheriou/Arnold 2005).
 *
 * Bits are connected one edge at a time. Connecting bit v to check j closes a cycle of length
 * dist(v, j) + 1, where dist is the current distance in the Tanner graph (bit -> check
 * distances are odd: 1 = already connected, 3 = would close a 4-cycle, 5 = a 6-cycle).
 * For every new edge we therefore pick a check that
 *   1. still has room (fewer than 6 bits)            -> rows end up with exactly 6 ones,
 *   2. is NOT within distance 5 of v                 -> no 4- or 6-cycle (girth 8),
 *      or, if no such check exists, not within 3     -> at least no 4-cycle (girth 6),
 *   3. has the fewest edges so far, ties broken by the seeded RNG.
 * If no check satisfies 1 + "not within 3", or the finished H is not of full rank, the attempt
 * is thrown away and the next one starts from the RNG's current state (still deterministic).
 * ------------------------------------------------------------------------------------------ */

typedef struct {
    int rowdeg[LDPC_M];
    int coldeg[LDPC_N];
} graph_fill_t;

/* near[j] = 1 + (distance from bit v to check j - 1) / 2 for checks within distance 5 of v,
 * i.e. near = 1 (distance 1), 2 (distance 3), 3 (distance 5); 0 = farther away. */
static void mark_near(const ldpc_code_t *c, const graph_fill_t *g, int v, uint8_t *near) {
    uint8_t seen_bit[LDPC_N] = {0};
    memset(near, 0, LDPC_M);
    seen_bit[v] = 1;
    for (int e = 0; e < g->coldeg[v]; e++) near[c->col[v][e]] = 1;
    for (int level = 1; level < 3; level++)          /* checks at level -> bits -> checks */
        for (int j = 0; j < LDPC_M; j++) {
            if (near[j] != level) continue;
            for (int s = 0; s < g->rowdeg[j]; s++) {
                int b = c->row[j][s];
                if (seen_bit[b]) continue;
                seen_bit[b] = 1;
                for (int e = 0; e < g->coldeg[b]; e++)
                    if (!near[c->col[b][e]]) near[c->col[b][e]] = (uint8_t)(level + 1);
            }
        }
}

/* Chooses the check for the next edge of bit v, or -1 if none is allowed. */
static int pick_check(const ldpc_code_t *c, const graph_fill_t *g, int v, rng_t *r) {
    uint8_t near[LDPC_M];
    mark_near(c, g, v, near);
    for (int min_dist_level = 0; min_dist_level <= 2; min_dist_level += 2) {
        /* pass 1: near == 0 (distance > 5); pass 2: near == 0 or 3 (distance > 3) */
        int best = -1, best_deg = LDPC_WR, ties = 0;
        for (int j = 0; j < LDPC_M; j++) {
            int ok = near[j] == 0 || (min_dist_level == 2 && near[j] == 3);
            if (!ok || g->rowdeg[j] >= LDPC_WR) continue;
            if (g->rowdeg[j] < best_deg) { best = j; best_deg = g->rowdeg[j]; ties = 1; }
            else if (g->rowdeg[j] == best_deg && rng_below(r, (uint32_t)++ties) == 0) best = j;
        }
        if (best >= 0) return best;      /* reservoir sampling: uniform among the ties */
    }
    return -1;
}

static int build_graph(ldpc_code_t *c, rng_t *r) {
    graph_fill_t g;
    memset(&g, 0, sizeof g);
    for (int v = 0; v < LDPC_N; v++)
        for (int e = 0; e < LDPC_WC; e++) {
            int j = pick_check(c, &g, v, r);
            if (j < 0) return -1;
            c->col[v][g.coldeg[v]++] = (uint16_t)j;
            c->row[j][g.rowdeg[j]++] = (uint16_t)v;
        }
    return 0;
}

/* ------------------------------------------------------------------------------------------
 * Systematic encoder by Gaussian elimination over GF(2) (see ldpc.h).
 * Row-reduce a dense copy of H; every pivot column becomes a parity position.
 * Returns 0 if H has full rank m, -1 otherwise.
 * ------------------------------------------------------------------------------------------ */
static int build_encoder(ldpc_code_t *c) {
    static uint8_t h[LDPC_M][LDPC_N];   /* 32 kB: static, not on the stack */
    uint8_t is_pivot[LDPC_N] = {0};
    memset(h, 0, sizeof h);
    for (int i = 0; i < LDPC_N; i++)
        for (int e = 0; e < LDPC_WC; e++) h[c->col[i][e]][i] = 1;

    int rank = 0;
    for (int col = 0; col < LDPC_N && rank < LDPC_M; col++) {
        int p = rank;
        while (p < LDPC_M && !h[p][col]) p++;
        if (p == LDPC_M) continue;                  /* no pivot here: an information column */
        if (p != rank) {                            /* swap rows p and rank */
            uint8_t tmp[LDPC_N];
            memcpy(tmp, h[p], LDPC_N); memcpy(h[p], h[rank], LDPC_N); memcpy(h[rank], tmp, LDPC_N);
        }
        for (int q = 0; q < LDPC_M; q++)            /* clear the column everywhere else: */
            if (q != rank && h[q][col])             /* row_q += row_rank (XOR = GF(2) add) */
                for (int x = 0; x < LDPC_N; x++) h[q][x] ^= h[rank][x];
        c->par_pos[rank] = (uint16_t)col;
        is_pivot[col] = 1;
        rank++;
    }
    if (rank < LDPC_M) return -1;                   /* dependent checks: k would exceed 128 */

    int k = 0;
    for (int col = 0; col < LDPC_N; col++)
        if (!is_pivot[col]) c->info_pos[k++] = (uint16_t)col;
    /* reduced row j:  x[par_pos[j]] + sum_i h[j][info_pos[i]] x[info_pos[i]] = 0 */
    for (int j = 0; j < LDPC_M; j++)
        for (int i = 0; i < LDPC_K; i++) c->A[j][i] = h[j][c->info_pos[i]];
    return 0;
}

int ldpc_build(ldpc_code_t *c, uint64_t seed) {
    rng_t r;
    rng_seed(&r, rng_derive(seed, "ldpc"));
    for (int attempt = 1; attempt <= 1000; attempt++) {
        memset(c, 0, sizeof *c);
        if (build_graph(c, &r) == 0 && build_encoder(c) == 0) {
            c->attempts = attempt;
            c->girth = ldpc_girth(c);
            return 0;
        }
    }
    memset(c, 0, sizeof *c);
    return -1;
}

const ldpc_code_t *ldpc_default(void) {
    static ldpc_code_t code;
    static int ready = 0;
    if (!ready) { ldpc_build(&code, LDPC_SEED); ready = 1; }
    return &code;
}

/* ------------------------------------------------------------------------------------------
 * Encoding and syndrome
 * ------------------------------------------------------------------------------------------ */

void ldpc_encode(const ldpc_code_t *c, const uint8_t *data, uint8_t *code) {
    for (int i = 0; i < LDPC_K; i++) code[c->info_pos[i]] = data[i];   /* systematic part */
    for (int j = 0; j < LDPC_M; j++) {
        uint8_t p = 0;
        for (int i = 0; i < LDPC_K; i++) p ^= (uint8_t)(c->A[j][i] & data[i]);
        code[c->par_pos[j]] = p;
    }
}

/* s_j = XOR of the 6 bits of check j (row j of H times the word, mod 2). */
int ldpc_syndrome(const ldpc_code_t *c, const uint8_t *word, uint8_t *s) {
    int unsatisfied = 0;
    for (int j = 0; j < LDPC_M; j++) {
        uint8_t x = 0;
        for (int e = 0; e < LDPC_WR; e++) x ^= word[c->row[j][e]];
        if (s) s[j] = x;
        unsatisfied += x;
    }
    return unsatisfied;
}

/* ------------------------------------------------------------------------------------------
 * Gallager bit-flipping decoder (hard decision, parallel "flip the worst bits")
 * ------------------------------------------------------------------------------------------ */

int ldpc_decode_iter(const ldpc_code_t *c, uint8_t *code, uint8_t *data, int max_iter, int *iters) {
    uint8_t received[LDPC_N], s[LDPC_M];
    memcpy(received, code, LDPC_N);
    int it = 0, unsatisfied = ldpc_syndrome(c, code, s);
    while (unsatisfied > 0 && it < max_iter) {
        /* votes[i] = how many of bit i's 3 checks fail (0..3) */
        int votes[LDPC_N], most = 0;
        for (int i = 0; i < LDPC_N; i++) {
            votes[i] = s[c->col[i][0]] + s[c->col[i][1]] + s[c->col[i][2]];
            if (votes[i] > most) most = votes[i];
        }
        for (int i = 0; i < LDPC_N; i++)            /* flip every bit with the most votes */
            if (votes[i] == most) code[i] ^= 1;
        it++;
        unsatisfied = ldpc_syndrome(c, code, s);
    }
    if (iters) *iters = it;
    if (unsatisfied > 0) memcpy(code, received, LDPC_N);   /* failure: leave the word as received */
    for (int i = 0; i < LDPC_K; i++) data[i] = code[c->info_pos[i]];
    return unsatisfied > 0 ? -1 : (int)bits_distance(received, code, LDPC_N);
}

int ldpc_decode(const ldpc_code_t *c, uint8_t *code, uint8_t *data) {
    return ldpc_decode_iter(c, code, data, LDPC_MAX_ITER, NULL);
}

/* ------------------------------------------------------------------------------------------
 * Girth: breadth-first search from every node of the Tanner graph (nodes 0..n-1 = bits,
 * n..n+m-1 = checks). Reaching an already-visited node w from u (w not u's parent) closes a
 * cycle of length dist[u] + dist[w] + 1; the minimum over all roots is the girth.
 * ------------------------------------------------------------------------------------------ */

#define NODES (LDPC_N + LDPC_M)

static int neighbours(const ldpc_code_t *c, int u, int *out) {
    if (u < LDPC_N) {
        for (int e = 0; e < LDPC_WC; e++) out[e] = LDPC_N + c->col[u][e];
        return LDPC_WC;
    }
    for (int e = 0; e < LDPC_WR; e++) out[e] = c->row[u - LDPC_N][e];
    return LDPC_WR;
}

int ldpc_girth(const ldpc_code_t *c) {
    int best = 0;
    for (int root = 0; root < NODES; root++) {
        int dist[NODES], parent[NODES], queue[NODES], head = 0, tail = 0;
        for (int i = 0; i < NODES; i++) dist[i] = -1;
        dist[root] = 0; parent[root] = -1; queue[tail++] = root;
        while (head < tail) {
            int u = queue[head++], nb[LDPC_WR];
            if (best && 2 * dist[u] >= best) break;   /* any new cycle has length >= 2 dist[u] */
            int deg = neighbours(c, u, nb);
            for (int e = 0; e < deg; e++) {
                int w = nb[e];
                if (dist[w] < 0) { dist[w] = dist[u] + 1; parent[w] = u; queue[tail++] = w; }
                else if (w != parent[u]) {
                    int len = dist[u] + dist[w] + 1;
                    if (!best || len < best) best = len;
                }
            }
        }
    }
    return best;
}
