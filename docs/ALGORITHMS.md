# Algorithms

The mathematics behind every module, each with a worked example. **Every number in the examples
was produced by running this repository's library** (`build/libfeclink.a`) or read from the CSVs
written by `make bench`; nothing was computed by hand.

**How the examples were produced.** A throwaway C program (not committed) that links the library:

```sh
make                                   # builds build/libfeclink.a
cc -std=c11 -O2 -D_POSIX_C_SOURCE=200809L -I include ex.c build/libfeclink.a -lm -o ex
./ex <section>                         # prints the example of that section
```

Each section names the library functions the program called. The "tag" used throughout is the
first tag the library draws from seed 42: `rng_seed(&r, 42); tag_random(&r, id)`.
CSV-based examples use `results/*.csv` from `make bench` at the commit of this document.

Contents: [1 Frame and code rate](#1-frame-structure-and-code-rate) ·
[2 CRC-32](#2-crc-32) · [3 Hamming and SECDED](#3-hamming74--hamming1511-and-secded84) ·
[4 GF(2^m)](#4-galois-fields-gf2m) · [5 BCH](#5-bch-codes) · [6 Reed-Solomon](#6-reed-solomon) ·
[7 Interleaver](#7-block-interleaver) · [8 LDPC](#8-ldpc-and-bit-flipping) ·
[9 Noise models](#9-noise-models) · [10 Failure prediction](#10-failure-prediction-used-by-the-adaptive-controller) ·
[11 Metrics](#11-metrics) · [12 Statistics](#12-statistics-over-seeds) ·
[13 Adaptive controller](#13-adaptive-controller-rules) · [Symbols](#symbols) · [Formulas vs code](#formulas-vs-code)

---

## 1. Frame structure and code rate

**Definition.** The payload is the 96-bit tag ID followed by its CRC-32: $K = 96 + 32 = 128$ bits.
A block code $(n, k)$ is applied to consecutive $k$-bit chunks; the last chunk is zero-padded.

$$
\text{blocks} = \left\lceil \frac{K}{k} \right\rceil, \qquad
\text{coded bits} = \text{blocks} \cdot n, \qquad
R_{\text{nominal}} = \frac{k}{n}, \qquad
R_{\text{eff}} = \frac{K}{\text{blocks} \cdot n}
$$

The effective rate includes the padding (DD-08) and is the `code_rate` column of every CSV.
Reed-Solomon and LDPC encode the whole payload as one codeword, so they have no padding. The
16-byte header is not counted (it is the same for every code, DD-21).

**Worked example.**

| code | n | k | blocks | coded bits | padding bits | $k/n$ | $R_\text{eff}$ |
|---|---|---|---|---|---|---|---|
| Hamming(7,4) | 7 | 4 | 32 | 224 | 0 | 0.5714 | 0.5714 |
| Hamming(15,11) | 15 | 11 | 12 | 180 | 4 | 0.7333 | 0.7111 |
| SECDED(8,4) | 8 | 4 | 32 | 256 | 0 | 0.5000 | 0.5000 |
| BCH(15,7) | 15 | 7 | 19 | 285 | 5 | 0.4667 | 0.4491 |
| BCH(31,16) | 31 | 16 | 8 | 248 | 0 | 0.5161 | 0.5161 |
| RS(24,16) t=4 | 192 bits | 128 bits | 1 | 192 | 0 | 0.6667 | 0.6667 |
| RS(32,16) t=8 | 256 bits | 128 bits | 1 | 256 | 0 | 0.5000 | 0.5000 |
| LDPC(256,128) | 256 | 128 | 1 | 256 | 0 | 0.5000 | 0.5000 |

E.g. Hamming(7,4): $128/4 = 32$ blocks of 7 bits = 224 coded bits, $R = 128/224 = 0.5714$;
RS(24,16): 16 data bytes + 8 parity bytes = 24 bytes = 192 bits, $R = 16/24 = 0.6667$.

*How this example was produced:* `./ex 1` - `fec_config`, `fec_block_n`, `fec_block_k`,
`fec_coded_bits`, `fec_code_rate` for every registered code (RS t=8 with `rs_t = 8`).

## 2. CRC-32

**Definition.** IEEE 802.3 CRC-32 with generator polynomial

$$
G(x) = x^{32} + x^{26} + x^{23} + x^{22} + x^{16} + x^{12} + x^{11} + x^{10} + x^{8} + x^{7} + x^{5} + x^{4} + x^{2} + x + 1
$$

(`0x04C11DB7`, bit-reflected `0xEDB88320`). The checksum is the remainder of the message
polynomial times $x^{32}$ divided by $G(x)$ over GF(2), with the register initialised to and
finally XORed with `0xFFFFFFFF`:

$$
\mathrm{CRC}(m) = \left( m(x) \cdot x^{32} \bmod G(x) \right) \oplus \texttt{0xFFFFFFFF} \quad \text{(reflected, init 0xFFFFFFFF)}
$$

The CRC is computed bitwise (`src/crc32.c`, no table) over the 12 ID bytes; the 4 CRC bytes are
appended big-endian. **Why bursts are detected:** the CRC is linear, so an error pattern $e$ is
missed only if $G(x)$ divides $e(x)$. A burst of length $L \le 32$ is $e(x) = x^i b(x)$ with
$\deg b = L-1 < 32$ and $b(0) = 1$; $G$ shares no factor with $x^i$ (its constant term is 1) and
cannot divide the lower-degree $b$, so the remainder is never zero. A random wrong word passes
with probability about $2^{-32}$. The CRC only *detects*; it never corrects.

**Worked example.**

| item | value |
|---|---|
| standard check value `crc32("123456789")` | `CBF43926` (matches the IEEE 802.3 reference) |
| tag ID (12 bytes) | `15 61 AE EC FD C5 B8 D9 C2 95 AE 4A` |
| CRC-32 of the tag | `0x1C81051E` |
| 128-bit payload (16 bytes) | `15 61 AE EC FD C5 B8 D9 C2 95 AE 4A` **`1C 81 05 1E`** |
| all 128 single-bit, 8128 double-bit and 341,376 triple-bit errors of the payload | 0 undetected (exhaustive) |
| 720,000 random bursts, every length 1..32 at every start position (200 random inner patterns each) | 0 undetected |

**Caveat found while writing this document (honest).** The burst guarantee above holds in the
CRC's own bit order. This repository computes a *reflected* CRC (bits processed LSB-first within
each byte) but transmits the payload MSB-first (`bits_pack`), and the CRC bytes are appended
big-endian. In transmission order the guarantee is therefore **not** exact: a GF(2) rank check of
every 32-bit window of the 128-bit payload found 54 of 97 windows with full rank 32 (minimum
rank 30), and the shortest undetected burst is **30 bits long** (start bit 81, weight 18;
confirmed with `frame_parse_payload` on the tag above: CRC passes, ID wrong). Such a pattern
needs one exact 18-bit arrangement, so it is extremely unlikely under the noise models, and
every 1-, 2- and 3-bit error is still detected; SILENT_WRONG stayed 0 in every run. Listed in
[DEVELOPMENT.md](DEVELOPMENT.md#open-problems); a fix would change the wire format.

*How this example was produced:* `./ex 2` - `crc32_compute`, `frame_build_payload`, `bits_pack`,
`frame_parse_payload` on flipped copies; the rank check and the shortest burst come from two more
throwaway programs that compute the linear syndrome
$s(e) = \mathrm{CRC}(e_\text{ID}) \oplus \mathrm{CRC}(0) \oplus e_\text{CRC field}$ of every
single-bit error with `crc32_compute` and run Gaussian elimination over GF(2).

## 3. Hamming(7,4) / Hamming(15,11) and SECDED(8,4)

**Definition.** Hamming$(2^r-1, 2^r-1-r)$, positions numbered $1..n$. Parity bits sit at the
power-of-two positions (1, 2, 4, 8); parity bit $2^j$ makes the XOR of all positions whose index
has bit $j$ set equal to 0. Hence for a received word $r$

$$
s = \bigoplus_{p \,:\, r_p = 1} p \qquad (\text{XOR of the positions of all ones})
$$

is 0 for a codeword and equals $p$ when exactly bit $p$ was flipped. Minimum distance 3: one
error is corrected; two errors give $s = p_1 \oplus p_2 \ne 0$, which points at a *third*
position, so the decoder "corrects" the wrong bit (miscorrection). The CRC-32 then catches it.

**Worked example - Hamming(7,4), data 1011:**

| step | word (positions 1..7) | syndrome | result |
|---|---|---|---|
| encode 1011 | `0110011` | 0 | parity at 1, 2, 4; data `1 0 1 1` at 3, 5, 6, 7 |
| flip position 5 | `0110111` | 5 | decoder flips bit 5 -> `0110011`, data `1011`, 1 bit changed |
| flip positions 2 and 5 | `0010111` | 7 ($= 2 \oplus 5$) | decoder flips bit 7 -> `0010110`, data **`1110`** (wrong), now 3 bits from the sent word |

The same 2-bit error placed in block 0 of a full Hamming(7,4) frame: the decoder reports
`corrected_bits = 1`, `failed_blocks = 0` (Hamming cannot detect it), but the CRC fails, so the
frame is classified **DETECTED_FAIL**, not SILENT_WRONG.

Hamming(15,11): data `10110010111` -> codeword `101101100010111`; flipping position 13 gives
syndrome 13.

**SECDED(8,4)** appends an overall parity bit $c_8 = c_1 \oplus \dots \oplus c_7$ (distance 4).
With syndrome $s$ of the first 7 bits and overall parity $P$ of all 8 received bits:

| $s$ | $P$ | meaning | action |
|---|---|---|---|
| 0 | 0 | no error | accept |
| $\ne 0$ | 1 | single error at position $s$ | correct |
| $\ne 0$ | 0 | double error | **detect** (decoder returns -1) |
| 0 | 1 | error in the parity bit $c_8$ | correct $c_8$ |

Worked example, data 1011 -> codeword `01100110`:

| received | $s$ | $P$ | `secded84_decode` returns | data |
|---|---|---|---|---|
| no error | 0 | 0 | 0 | 1011 |
| 1 error at position 5 | 5 | 1 | 1 | 1011 |
| 2 errors at positions 2 and 5 | 7 | 0 | -1 (detected) | 1111 (received bits, flagged) |
| error in position 8 | 0 | 1 | 1 | 1011 |

*How this example was produced:* `./ex 3` - `hamming_encode`, `hamming_syndrome`,
`hamming_decode` (r = 3 and r = 4), `fec_encode`/`fec_decode` + `frame_parse_payload` +
`frame_classify` for the frame, `secded84_encode`/`secded84_decode`.

## 4. Galois fields GF(2^m)

**Definition.** GF($2^m$) = binary polynomials of degree $< m$ modulo a primitive polynomial
$p(x)$; addition is XOR. $\alpha = x$ generates all $2^m - 1$ non-zero elements, so with tables
$\exp[i] = \alpha^i$ and $\log[\alpha^i] = i$:

$$
a \cdot b = \alpha^{(\log a + \log b) \bmod (2^m-1)}, \qquad a^{-1} = \alpha^{(2^m - 1 - \log a) \bmod (2^m-1)}
$$

The repo uses GF(16) with $p(x) = x^4 + x + 1$ (BCH(15,7)), GF(32) with $x^5 + x^2 + 1$ (BCH(31,16))
and GF(256) with $x^8 + x^4 + x^3 + x^2 + 1$ = `0x11D` (Reed-Solomon).

**Worked example - GF(16), $p(x) = x^4 + x + 1$** (each power is the previous one shifted left,
reduced with $x^4 = x + 1$):

| $i$ | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| $\alpha^i$ (decimal) | 1 | 2 | 4 | 8 | 3 | 6 | 12 | 11 | 5 | 10 | 7 | 14 | 15 | 13 | 9 |
| $\alpha^i$ (binary) | 0001 | 0010 | 0100 | 1000 | 0011 | 0110 | 1100 | 1011 | 0101 | 1010 | 0111 | 1110 | 1111 | 1101 | 1001 |

Multiplication $6 \cdot 11$: $\log 6 = 5$, $\log 11 = 7$, so $6 \cdot 11 = \alpha^{12} = 15$
(`gf_mul` gives 15). Inverse: $6^{-1} = 7$ ($6 \cdot 7 = 1$).
GF(256), `0x11D`: $\alpha^8 = $ `0x1D`, and `0x53` $\cdot$ `0xCA` $= \alpha^{206+73} =$ `0x8F`.

*How this example was produced:* `./ex 4` - `gf_init(&f, 4, 0x13)`, `gf_exp`, `gf_log`, `gf_mul`,
`gf_inv`; `gf_init(&g, 8, 0x11D)`.

## 5. BCH codes

**Definition.** Narrow-sense primitive binary BCH code of length $n = 2^m - 1$ correcting $t$
errors:

$$
g(x) = \mathrm{lcm}\left( M_1(x), M_2(x), \dots, M_{2t}(x) \right), \qquad k = n - \deg g
$$

where $M_j$ is the minimal polynomial of $\alpha^j$. Every codeword satisfies $c(\alpha^j) = 0$
for $j = 1..2t$, so (BCH bound) $d_\text{min} \ge 2t + 1$. Systematic encoding:
$c(x) = x^{n-k} d(x) + \left[ x^{n-k} d(x) \bmod g(x) \right]$. Decoding $r(x) = c(x) + e(x)$:

1. **Syndromes** $S_j = r(\alpha^j) = e(\alpha^j) = \sum_l X_l^{\,j}$, $j = 1..2t$, with error locators $X_l = \alpha^{\text{pos}_l}$.
2. **Berlekamp-Massey** finds the shortest LFSR that generates $S_1..S_{2t}$; its connection
   polynomial is the error-locator $\Lambda(x) = \prod_l (1 + X_l x)$.
3. **Chien search** tries every position $i$: $\Lambda(\alpha^{-i}) = 0 \iff$ error at $i$.
4. Binary code: the error value is 1, flip the bits. If the number of roots differs from
   $\deg \Lambda$ (or $\deg\Lambda > t$) the decoder reports a detected failure.

**Worked example - BCH(15,7), t = 2, two errors.** The library builds
$g(x) = 1 + x^4 + x^6 + x^7 + x^8$ (coefficients $x^0..x^8$ = `100010111`), i.e.
$g(x) = x^8 + x^7 + x^6 + x^4 + 1$, $k = 15 - 8 = 7$.

| step | value |
|---|---|
| data $d_0..d_6$ | `1011001` |
| codeword $c_0..c_{14}$ (parity $c_0..c_7$, data $c_8..c_{14}$) | `010000111011001` |
| errors at positions 3 and 10, received | `010100111001001` |
| $S_1$ | 15 $= \alpha^{12}$ $(= \alpha^3 + \alpha^{10})$ |
| $S_2$ | 10 $= \alpha^{9}$ $(= \alpha^6 + \alpha^{5})$ |
| $S_3$ | 11 $= \alpha^{7}$ $(= \alpha^9 + \alpha^{0})$ |
| $S_4$ | 8 $= \alpha^{3}$ $(= \alpha^{12} + \alpha^{10})$ |
| Berlekamp-Massey | $L = 2$, $\Lambda(x) = 1 + \alpha^{12} x + \alpha^{13} x^2$ |
| check | $(1 + \alpha^3 x)(1 + \alpha^{10} x)$: $\alpha^3 + \alpha^{10} = \alpha^{12}$, $\alpha^3 \alpha^{10} = \alpha^{13}$ |
| Chien search | $\Lambda(\alpha^{-3}) = 0$, $\Lambda(\alpha^{-10}) = 0$ -> positions 3 and 10 |
| `bch_decode` | returns 2 (bits corrected), repaired word = sent codeword |

*How this example was produced:* `./ex 5` - `bch157()`, `bch_encode`, `bch_syndromes`,
`bch_berlekamp_massey`, `bch_chien`, `bch_decode`; the $\Lambda(\alpha^{-i})$ check evaluates
$\Lambda$ with `gf_mul`/`gf_exp`.

## 6. Reed-Solomon

**Definition.** RS over GF(256) with byte symbols, shortened to RS($16 + 2t$, 16) (one codeword
per frame, DD-10), roots $\alpha^1..\alpha^{2t}$:

$$
g(x) = \prod_{i=1}^{2t} \left( x + \alpha^i \right)
$$

Systematic: parity $= x^{2t} d(x) \bmod g(x)$ in `cw[0..2t-1]`, data bytes in `cw[2t..n-1]`.
Decoding uses the same syndromes $S_j = r(\alpha^j)$, Berlekamp-Massey (shared with BCH) and Chien
search; the error *values* (bytes) come from **Forney's formula**:

$$
\Omega(x) = S(x)\,\Lambda(x) \bmod x^{2t}, \quad S(x) = S_1 + S_2 x + \dots + S_{2t} x^{2t-1}, \qquad
e_i = \frac{\Omega(X_i^{-1})}{\Lambda'(X_i^{-1})}, \quad X_i = \alpha^{i}
$$

($\Lambda'$ is the formal derivative; in characteristic 2 only the odd-power terms survive.)
RS corrects any $t$ wrong **bytes**. A burst of $L$ bits touches at most
$\lceil (L + 7)/8 \rceil$ bytes, so every burst of

$$
L \le 8(t - 1) + 1 \text{ bits}
$$

is corrected: $t = 4$ -> 25 bits.

**Worked example - RS(24,16) t = 4, two corrupted bytes** (payload = the 16 bytes of section 2):

| step | value |
|---|---|
| $g(x)$ coefficients $x^0..x^8$ | `25 E0 08 AC 47 B2 2C E3 01` |
| codeword `cw[0..23]` | `1B F9 E4 C1 B7 97 C3 AD` (parity) `15 61 AE EC FD C5 B8 D9 C2 95 AE 4A 1C 81 05 1E` (data) |
| corruption | `cw[10]` ^= `5A` (`AE` -> `F4`), `cw[19]` ^= `FF` (`4A` -> `B5`) |
| syndromes $S_1..S_8$ | `02 C7 7B 0A 09 BE C9 E3` |
| Berlekamp-Massey | $L = 2$, $\Lambda$ coefficients `01 2E 30` |
| $\Omega(x) = S\Lambda \bmod x^8$ | coefficients `02 9B 00 00 00 00 00 00` |
| Chien root $X = \alpha^{10}$ | $\Omega(X^{-1}) =$ `A4`, $\Lambda'(X^{-1}) =$ `2E` -> $e_{10} =$ **`5A`** |
| Chien root $X = \alpha^{19}$ | $\Omega(X^{-1}) =$ `05`, $\Lambda'(X^{-1}) =$ `2E` -> $e_{19} =$ **`FF`** |
| `rs_decode` | returns 2, repaired word = sent codeword |

Burst bound, measured: solid bursts (all $L$ bits flipped) at every start position of the
192-bit RS t=4 frame: $L = 24$: 169 of 169 corrected; $L = 25$: 168 of 168; $L = 26$: 147 of 167
(a 26-bit burst can touch 5 bytes); $L = 27$: 126 of 166.

*How this example was produced:* `./ex 6` - `rs_get(16, 4)`, `rs_encode`, `rs_syndromes`,
`bch_berlekamp_massey`; $\Omega$, Chien and Forney evaluated with `gf_mul`/`gf_div`/`gf_pow`
(and checked against `rs_decode`, which gives the same two corrections); the burst sweep uses
`fec_encode`/`fec_decode` with `CODE_RS`.

## 7. Block interleaver

**Definition.** The $n$ coded bits are written **row by row** into a matrix with $D$ rows
(depth) and $C = \lceil n / D \rceil$ columns, and sent **column by column**. Bit $j$ of the
codeword at row $\lfloor j / C \rfloor$, column $j \bmod C$ is sent as number
$(j \bmod C) \cdot D + \lfloor j / C \rfloor$ (when $D$ divides $n$). Two bits adjacent on the air
were $C$ positions apart in the codeword, so a burst of up to $D$ bits hits $D$ different rows;
with $C \ge n_\text{block}$ each code block sees at most one error of that burst (DD-11;
guaranteed when $D$ divides $n$).

**Worked example 1 - depth 2, n = 6:** rows `[0 1 2]` / `[3 4 5]` -> sent order `0 3 1 4 2 5`;
de-interleaving gives `0 1 2 3 4 5` back.

**Worked example 2 - burst spreading, Hamming(7,4) frame (224 bits), burst on air bits 100..107:**

| depth | codeword positions hit (block) | max errors in one block | frame |
|---|---|---|---|
| 1 (off) | 100-104 (block 14), 105-107 (block 15) | 5 | not CORRECT |
| 8 ($C = 28$) | 13 (b1), 41 (b5), 69 (b9), 97 (b13), 124 (b17), 152 (b21), 180 (b25), 208 (b29) | 1 | CORRECT |

With depth 8 the 8-bit burst becomes 8 single errors, 28 positions apart, in 8 different
Hamming blocks, and every block corrects its one error.

*How this example was produced:* `./ex 7` - `interleave`/`deinterleave`; the burst example
encodes with `fec_encode` (Hamming(7,4)), flips air bits 100..107, de-interleaves, decodes with
`fec_decode` and checks the ID with `frame_parse_payload`.

## 8. LDPC and bit flipping

**Definition.** A linear code given by a sparse parity-check matrix $H$ ($m \times n$):

$$
c \text{ is a codeword} \iff H c^{T} = 0 \pmod 2, \qquad s = H r^{T} = H (c + e)^{T} = H e^{T}
$$

so the syndrome depends only on the error. The repo's code is (3,6)-regular LDPC(256,128): every
column of $H$ has 3 ones, every row 6 (DD-25..DD-27). **Gallager bit flipping** (DD-28):
(1) compute $s$; stop with success if $s = 0$; (2) for every bit count its unsatisfied checks;
(3) flip all bits with the maximum count; repeat, at most 50 iterations, else report a detected
failure.

**Worked example 1 - a toy code (illustration only; not the code used by the project).**
A (2,3)-regular $H$ with 4 checks and 6 bits; each column is a different pair of checks, so no two
bits share two checks (no 4-cycles):

$$
H = \begin{pmatrix}
1 & 1 & 1 & 0 & 0 & 0 \\
1 & 0 & 0 & 1 & 1 & 0 \\
0 & 1 & 0 & 1 & 0 & 1 \\
0 & 0 & 1 & 0 & 1 & 1
\end{pmatrix}
$$

Its null space has 8 codewords (rank 3, $k = 3$): `000000 000111 011001 011110 101010 101101
110011 110100`. Send `110100`, flip bit 4:

| iteration | received $r$ | syndrome $s$ | unsatisfied-check count per bit | action |
|---|---|---|---|---|
| 0 | `110000` | `0110` | `1 1 0 2 1 1` | flip bits with count 2 (bit 4) |
| 1 | `110100` | `0000` | - | codeword, stop (success) |

**Worked example 2 - the real LDPC(256,128)** (seed 2026, built on attempt 1, girth 6). The tag
payload of section 2 is encoded (syndrome weight 0) and 1, 2 or 3 bits (positions 5, 77, 200)
are flipped:

| errors | unsatisfied checks | `ldpc_decode_iter` returns | iterations | data recovered |
|---|---|---|---|---|
| 1 | 3 | 1 | 1 | yes |
| 2 | 6 | 2 | 1 | yes |
| 3 | 9 | 3 | 1 | yes |

(Each bit sits in 3 checks, so $w$ isolated errors leave $3w$ unsatisfied checks.)

*How this example was produced:* `./ex 8` - toy: the program enumerates the 64 words against
the toy $H$ and runs the bit-flipping rule; real code: `ldpc_default`, `ldpc_encode`,
`ldpc_syndrome`, `ldpc_decode_iter`.

## 9. Noise models

**BSC.** Every bit flips independently with probability $p$; expected flips per frame $= p \cdot n$.

**Gilbert-Elliott.** Two-state Markov chain per bit: good state G (flip probability $e_G$), bad
state B ($e_B$); $G \to B$ with probability $p_{gb}$, $B \to G$ with $p_{bg}$. The state is kept
across frames and starts in the stationary distribution (DD-02):

$$
\pi_B = \frac{p_{gb}}{p_{gb} + p_{bg}}, \quad \pi_G = 1 - \pi_B, \qquad
\overline{\text{burst}} = \frac{1}{p_{bg}} \text{ bits}, \qquad
\text{BER} = \pi_G\, e_G + \pi_B\, e_B
$$

**Burst.** With probability $p_\text{frame}$ a frame gets one burst of length $L$ at a uniform
position; first and last bit flip, inner bits flip with probability `density` (DD-03), so the
expected number of flips per burst is $2 + (L - 2) \cdot \text{density}$.

**Worked example - the two profiles** (`include/profiles.h`; modelling assumptions, DD-05):

| profile | $p_{gb}$ | $p_{bg}$ | $e_G$ | $e_B$ | $\pi_B$ | mean burst | BER (formula) | BER measured, $10^6$ bits, seed 42 |
|---|---|---|---|---|---|---|---|---|
| TOLL_PLAZA | 0.002 | 0.5 | 0.005 | 0.5 | 0.003984 | 2.0 bits | 0.006972 | 0.006949 |
| HOSPITAL_IMAGING | 0.005 | 0.08 | 0.002 | 0.5 | 0.058824 | 12.5 bits | 0.031294 | 0.030538 |

Toll: $\pi_B = 0.002 / 0.502 = 0.003984$, BER $= 0.996016 \cdot 0.005 + 0.003984 \cdot 0.5 = 0.006972$.
BSC: toll $p = 0.01$, hospital $p = 0.02$. Burst: toll $L = 4$ in 20% of frames (expected 3 flips
per burst), hospital $L = 16$ in 50% of frames (expected 9 flips per burst), density 0.5.

*How this example was produced:* `./ex 9` - `profile_noise(..., NOISE_GE, -1)`,
`noise_expected_ber`, and `noise_init` (seed 42) + `noise_apply` on $10^6$ zero bits.

## 10. Failure prediction used by the adaptive controller

**Definition.** Binomial model of a random-error channel with bit error probability $p$
(DD-17). A block of $n$ symbols that corrects $t$ errors survives with

$$
P(\text{block ok}) = \sum_{i=0}^{t} \binom{n}{i} p^{i} (1-p)^{n-i}, \qquad
P_\text{fail} = 1 - P(\text{block ok})^{\text{blocks}}
$$

For Reed-Solomon the symbol is a byte, so $p$ is replaced by the byte error probability
$1 - (1 - p)^8$, $n = 16 + 2t$ bytes and one block. The controller steps DOWN only when this
prediction for the next cheaper code, at the measured $\widehat{\text{BER}}$, is below 0.05.

**Worked example - Hamming(7,4) at $p = 0.01$** ($n = 7$, $t = 1$, 32 blocks):

$$
P(\text{block ok}) = 0.99^7 + 7 \cdot 0.01 \cdot 0.99^6 = 0.997969, \qquad
P_\text{fail} = 1 - 0.997969^{32} = 0.062988
$$

`adapt_predict_fail` returns **0.062988** - identical. For comparison the benchmark measured a
frame failure rate of $1 - 0.9354 = 0.0646$ for Hamming(7,4) on toll BSC $p = 0.01$
(`results/bench.csv`, 5000 frames), close to the prediction.

| code at $p = 0.01$ | `adapt_predict_fail` |
|---|---|
| Hamming(7,4) | 0.062988 |
| BCH(15,7) | 0.007871 |
| BCH(31,16) | 0.002027 |
| RS t=4 (byte error probability 0.077255, $n = 24$, $t = 4$) | 0.033869 |

*How this example was produced:* `./ex 10` - the binomial sum computed directly in the program
and `adapt_predict_fail` for each configuration; the measured rate is the `frame_success_rate`
of row `hamming74,bsc,0.01` in `results/bench.csv`.

## 11. Metrics

| metric | formula | code |
|---|---|---|
| success rate | $\dfrac{\#\text{CORRECT}}{\#\text{frames}}$ | `linksim.c`, `sim.c` |
| goodput | $\dfrac{\#\text{CORRECT} \cdot 128}{\text{all coded bits transmitted (parity and retransmissions included)}}$ (DD-21) | `linksim.c` |
| retransmissions per frame | $\dfrac{\#\text{transmissions} - \#\text{frames}}{\#\text{frames}}$ | `linksim.c` |
| p99 latency | sort the delivery latencies $l_{(0)} \le \dots \le l_{(N-1)}$ of delivered frames; $p_{99} = l_{(\lfloor 0.99 (N-1) + 0.5 \rfloor)}$ (nearest rank) | `linksim.c` |

**Latency model (simulated clock, DD-13/DD-14).** Airtime of a transmission of $n$ coded bits at
160 kbit/s is $n / 160$ ms. The first copy arrives at airtime $+ \mathrm{RTT}/2 +$ decode time; each
retransmission waits for the NAK ($\mathrm{RTT}/2$) and travels again, so a frame delivered by its
$a$-th transmission has

$$
\text{latency} = a \cdot \frac{n}{160\ \text{kbit/s}} + \frac{\mathrm{RTT}}{2} + (a-1) \cdot \mathrm{RTT} + \text{decode time}
$$

**Worked examples** (`make bench`, seed 42, schedule toll:2000,hospital:2000,toll:2000, RTT 20 ms):

* Latency, Hamming(7,4), first transmission: $224 / 160 = 1.4$ ms $+ 10$ ms $= 11.4$ ms plus a few
  microseconds of decoding; `results/adaptive_trace.csv` frame 0: **11.402 ms**. HARQ + RS t=4
  (192 bits, 1.2 ms airtime): 1 retransmission -> $2.4 + 10 + 20 = 32.4$ ms, 2 retransmissions
  -> $3.6 + 10 + 40 = 53.6$ ms - exactly the two p99 values seen across seeds (DD-23).
* Goodput identity for a fixed code: goodput $=$ success $\cdot 128 / ((1 + \text{retx}) \cdot n)$.
  HARQ + RS t=4 (`bench_strategy.csv`): $1.000000 \cdot 128 / (1.0892 \cdot 192) = 0.6121$, CSV
  goodput **0.6121**. ARQ (uncoded, $n = 128$): $0.8194 / 2.7828 = 0.2945$, CSV **0.2945**.
* p99 by nearest rank: the adaptive run in `results/adaptive_trace.csv` delivered 4997 of 5000
  frames (3 DETECTED_FAIL after 5 transmissions); index $\lfloor 0.99 \cdot 4996 + 0.5 \rfloor = 4946$
  of the sorted latencies is **33.21 ms** (mean 13.064 ms), the same as the adaptive row of
  `bench_strategy.csv` (p99 33.210 ms, mean 13.065 ms).

*How this example was produced:* arithmetic on `results/bench_strategy.csv` and
`results/adaptive_trace.csv` (awk/sort), formulas read from `src/linksim.c`.

## 12. Statistics over seeds

**Definition.** For $N$ independent runs (seeds) with values $x_1..x_N$:

$$
\bar{x} = \frac{1}{N}\sum x_i, \qquad s = \sqrt{\frac{1}{N-1}\sum (x_i - \bar{x})^2}, \qquad
\bar{x} \pm t_{0.975,\,N-1} \frac{s}{\sqrt{N}}
$$

with Student's $t$ quantile ($N = 20$: $t_{0.975,19} = 2.093$; DD-23). **Paired differences:**
for strategies A and B on the same seeds, $d_i = x_i^{A} - x_i^{B}$ and the same interval is
computed for $\bar d$; the difference is significant when the interval excludes 0. Pairing
removes the channel luck both strategies share on a seed.

**Worked example - one real row of `results/bench_strategy_seeds.csv`:**

| strategy | channel | seeds | success | goodput | retx / frame | p99 (ms) |
|---|---|---|---|---|---|---|
| adaptive (mixed ladder) | schedule | 20 | 0.999490 +- 0.000088 | 0.504265 +- 0.002453 | 0.068940 +- 0.002138 | 33.2102 +- 0.0003 |

Recomputed with `stats_mean_ci` from the 20 per-seed goodput values in
`bench_strategy_seeds_raw.csv` (stored with 4 decimals; seed 42: 0.5047, 43: 0.5010, 44: 0.5103, ...):
$\bar x = 0.504280$, $s = 0.005230$, half-width $2.0930 \cdot 0.005230 / \sqrt{20} = 0.002447$ -
equal to the CSV row up to the rounding of the raw file (0.504265 +- 0.002453).

Paired difference adaptive minus HARQ + RS t=4, goodput (seed 42: $0.5047 - 0.6121 = -0.1074$,
43: $-0.1099$, 44: $-0.1044$, ...): recomputed $\bar d = -0.108280$, half-width $0.003031$;
`bench_strategy_seeds_diff.csv`: **-0.108288 +- 0.003034**, significant (interval
[-0.111322, -0.105254] excludes 0).

*How this example was produced:* `./ex 12 < values` (`stats_mean_ci`, `stats_t975`) on columns
of `results/bench_strategy_seeds_raw.csv` extracted with awk.

## 13. Adaptive controller rules

**Definition** (DD-15..DD-18). The tollgate keeps a window of the last $W = 32$ transmissions at
the current level and reports $f = \text{failures}/W$ and
$\widehat{\text{BER}} = \text{corrected bits}/\text{coded bits}$ (successful frames only) in every
ACK/NAK. The vehicle decides **once per $W$ fresh feedbacks** (disjoint windows, restarted at each
level change):

* **UP** if $f > 0.20$.
* **DOWN** if $f \le 0.05$ **and** $P_\text{fail}(\text{level} - 1, \widehat{\text{BER}}) < 0.05$
  (section 10) **and** the level has been held for at least $\text{hold} \cdot W$ frames.
* **Probe back-off:** a step down that must be undone within $2W$ frames doubles `hold`
  (1, 2, 4, ..., max 16 windows); a step down that survives $2W$ frames resets it to 1.
* Between the thresholds (0.05 < f <= 0.20) the level is held.

Mixed ladder: L0 none, L1 Hamming(7,4), L2 BCH(15,7), L3 BCH(31,16), L4 RS t=4, L5 RS t=8.

**Worked example - a real level change from `results/adaptive_trace.csv`** (seed 42, schedule
toll:2000, hospital:2000, toll:2000 transmissions; columns frame, first transmission `tx`,
segment, level at the frame's start, attempts, class, latency):

| frame | tx | segment | level | attempts | class | latency (ms) |
|---|---|---|---|---|---|---|
| 1847 | 1997 | TOLL_PLAZA | 1 | 1 | CORRECT | 11.402 |
| 1848 | 1998 | TOLL_PLAZA | 1 | 2 | CORRECT | 32.804 |
| 1849 | 2000 | HOSPITAL_IMAGING | 1 | 1 | CORRECT | 11.402 |
| 1850 | 2001 | HOSPITAL_IMAGING | 1 | 4 | CORRECT | 75.608 |
| 1851 | 2005 | HOSPITAL_IMAGING | 1 | 4 | CORRECT | 75.607 |
| 1852 | 2009 | HOSPITAL_IMAGING | 1 | 2 | CORRECT | 32.804 |
| 1853 | 2011 | HOSPITAL_IMAGING | 1 | 1 | CORRECT | 11.402 |
| 1854 | 2012 | HOSPITAL_IMAGING | 1 | 1 | CORRECT | 11.402 |
| 1855 | 2013 | HOSPITAL_IMAGING | 1 | 2 | CORRECT | 32.804 |
| 1856 | 2015 | HOSPITAL_IMAGING | 1 | 3 | CORRECT | 54.975 |
| **1857** | **2018** | HOSPITAL_IMAGING | **2** | 1 | CORRECT | 11.786 |

Reading (reconstructed from the trace with the rules above): level 1 has been held since
transmission 0, so decisions fall after every 32nd feedback; the one after transmission 2015
(the 2016th = $63 \cdot 32$) sees the window of transmissions 1984..2015, in which 10 failed
($f = 10/32 = 0.3125 > 0.20$) -> **UP to L2**; frame 1856's retransmissions are already sent at
L2, and frame 1857 starts at L2. All level changes of this run (8, as in `bench_strategy.csv`):

| frame | tx | change | segment |
|---|---|---|---|
| 1857 | 2018 | L1 -> L2 | hospital |
| 1867 | 2050 | L2 -> L3 | hospital |
| 1879 | 2080 | L3 -> L4 | hospital |
| 1931 | 2144 | L4 -> L5 | hospital |
| 3731 | 4032 | L5 -> L4 | toll |
| 3792 | 4096 | L4 -> L3 | toll |
| 3824 | 4128 | L3 -> L2 | toll |
| 3856 | 4160 | L2 -> L1 | toll |

The first step down (tx 4032) comes at the first decision after the toll segment returns at
transmission 4000: level 5 was entered at tx 2144, and $4032 - 2144 = 1888 = 59 \cdot 32$, so that
window is exactly transmissions 4000..4031 of the toll channel. Later steps are one or two
windows apart; no step happened at the decision points in between.

*How this example was produced:* rows of `results/adaptive_trace.csv` (awk), rules read from
`src/adapt.c` (`adapt_update`).

---

## Symbols

| symbol | meaning |
|---|---|
| $K$ | payload bits = 128 (96-bit ID + 32-bit CRC) |
| $n, k$ | block length and data length of a code (bits; bytes for RS) |
| $t$ | correctable errors per block (bits; bytes for RS) |
| $R$ | code rate; $R_\text{eff} = K / \text{coded bits}$ |
| $G(x)$ | CRC-32 generator polynomial |
| $s$ | syndrome |
| $\alpha$ | primitive element of GF($2^m$) |
| $p(x)$ | primitive polynomial of the field |
| $g(x)$ | generator polynomial of a BCH/RS code |
| $S_j$ | $j$-th syndrome $r(\alpha^j)$ |
| $\Lambda(x)$, $\Omega(x)$ | error-locator and error-evaluator polynomials |
| $X_l$ | error locator $\alpha^{\text{pos}_l}$ |
| $H$ | parity-check matrix (Hamming, LDPC) |
| $D$, $C$ | interleaver depth (rows) and columns |
| $p$ | BSC flip probability / bit error probability |
| $p_{gb}$, $p_{bg}$, $e_G$, $e_B$ | Gilbert-Elliott transition and flip probabilities |
| $\pi_G$, $\pi_B$ | stationary probabilities of the good / bad state |
| $L$ | burst length (bits) |
| $W$ | controller window (32 transmissions) |
| $f$ | measured window failure rate |
| $\widehat{\text{BER}}$ | estimated bit error rate from corrected bits |
| $\mathrm{RTT}$ | simulated round-trip time (20 ms default) |
| $N$, $\bar x$, $s$, $t_{0.975,N-1}$ | number of seeds, mean, sample standard deviation, Student-t quantile |

## Formulas vs code

| formula | file / function |
|---|---|
| chunking, effective rate | `src/fec.c`: `fec_coded_bits`, `fec_code_rate`, `fec_encode`, `fec_decode` |
| CRC-32 remainder | `src/crc32.c`: `crc32_update`, `crc32_compute`; payload layout `src/frame.c`: `frame_build_payload`, `frame_parse_payload` |
| Hamming syndrome $s = \bigoplus p$ | `src/hamming.c`: `hamming_encode`, `hamming_syndrome`, `hamming_decode` |
| SECDED table | `src/secded.c`: `secded84_encode`, `secded84_decode` |
| GF exp/log, multiplication | `src/gf.c`: `gf_init`, `gf_mul`, `gf_div`, `gf_inv`, `gf_pow` |
| BCH $g(x)$ = lcm of minimal polynomials | `src/bch.c`: `bch_init` |
| syndromes, Berlekamp-Massey, Chien | `src/bch.c`: `bch_syndromes`, `bch_berlekamp_massey`, `bch_chien`, `bch_decode` |
| RS $g(x)$, Forney | `src/rs.c`: `rs_get`, `rs_encode`, `rs_syndromes`, `rs_decode` |
| interleaver mapping | `src/interleave.c`: `interleave`, `deinterleave` |
| $H c^T = 0$, bit flipping | `src/ldpc.c`: `ldpc_build`, `ldpc_encode`, `ldpc_syndrome`, `ldpc_decode_iter` |
| BSC / Gilbert-Elliott / burst, $\pi_B$, BER | `src/noise.c`: `noise_apply`, `noise_expected_ber`; parameters `src/profiles.c` |
| binomial $P_\text{fail}$ | `src/adapt.c`: `adapt_predict_fail` |
| UP / DOWN / hold rules | `src/adapt.c`: `adapt_update`; window `src/window.c` |
| goodput, retx/frame, p99, latency model | `src/linksim.c`: `linksim_run`, `transmit` |
| mean +- Student-t CI | `src/stats.c`: `stats_mean_ci`, `stats_t975`; paired differences `tools/bench.c` |
