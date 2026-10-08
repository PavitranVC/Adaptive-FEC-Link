# DECISIONS

Lines starting with 🔀 are choices that could change the project's direction - please review them.
Format: what was chosen / the alternative / why.

## Phase 0

- 🔀 The frame header (seq number, code id, code parameters) is **never corrupted** by the channel;
  only the FEC-coded payload is. Alternative: corrupt the header too and protect it with its own
  code. Why: the assignment fixes this; it isolates the comparison of payload codes. A header
  corruption experiment is listed as week-2 failure-injection work.
- Library + three thin programs: all coding logic lives in `build/libfeclink.a` so the in-process
  benchmark and the UDP pipeline run exactly the same code.
- Bits are kept one-per-byte inside the coding layer (clarity over speed); packed only on the wire.
- Tiny home-made test helper (`tests/testlib.h`) instead of a framework - no external C libraries.
- `.gitignore` lists `bin/` literally (as required); `bin/.gitkeep` is force-added once.

## Phase 1

- CRC-32 is the standard IEEE 802.3 / zip CRC (reflected 0xEDB88320, init/xorout 0xFFFFFFFF),
  computed bitwise (no table) so the division can be explained line by line. It covers the
  12 ID bytes only; the 4 CRC bytes are appended big-endian -> 128-bit payload.
- Hamming(7,4) and Hamming(15,11) share one generic implementation, parameterised by r
  (n = 2^r - 1), with the textbook layout (parity at power-of-two positions, syndrome = error position).
- Block decoders return the number of corrected bits (>= 0) or a negative value when they
  *detect* an uncorrectable pattern (SECDED double error; later BCH/RS decoding failure).

## Phase 2

- RNG: xoshiro256** seeded by splitmix64, implemented in-repo (not `rand()`), so a given
  `--seed` is bit-identical across Linux/WSL/macOS. Each process derives its own sub-stream
  with `rng_derive(seed, "<role>")`.
- 🔀 Gilbert-Elliott channel state is **kept across frames** (one continuous interference process)
  and starts from the stationary distribution. Alternative: reset to "good" at every frame.
  Why: real interference does not know frame boundaries; resetting would under-estimate bursts.
- 🔀 Burst model = classic "burst of length L": first and last bit of the burst always flip, the
  bits in between flip with probability `density` (0.5 in both profiles), and a frame gets a
  burst with probability `p_frame`. Alternative: flip all L bits. Why: matches the textbook
  definition used for burst-correction guarantees (RS/interleaving) and is less artificial.
- 🔀 Meaning of `--p` per model: BSC -> flip probability p; GE -> p_gb (rate at which bursts
  start; mean burst length 1/p_bg and e_good/e_bad come from the profile); BURST -> probability a
  frame contains a burst (length from the profile, override with `--burst-len`).
  Alternative: one global "BER" knob. Why: each model keeps its physical meaning and the profile
  stays the single source of the other parameters.
- 🔀 Profile numbers (see `include/profiles.h`) are modelling assumptions, not measurements:
  TOLL_PLAZA BSC p=0.01, GE (0.002, 0.5, 0.005, 0.5) ~BER 0.007, burst L=4 in 20% of frames;
  HOSPITAL_IMAGING BSC p=0.02, GE (0.005, 0.08, 0.002, 0.5) ~BER 0.031 with 12.5-bit mean
  bursts, burst L=16 in 50% of frames. Default model: toll -> bsc, hospital -> ge.

## Phase 3

- 🔀 Ground truth travels as an **evaluation-only sidecar** that the channel appends to each
  packet (clean codeword, true tag ID, number of flipped bits, profile, model). The channel gets
  the true ID by decoding the *clean* codeword. Alternative: a separate truth log file joined
  offline by seq. Why: live classification in the tollgate during the demo, no file races; the
  tollgate's decoder never reads the sidecar (only `frame_classify` does).
- 🔀 A frame is DETECTED_FAIL if the CRC fails **or** a block decoder detected an uncorrectable
  pattern (SECDED double error, later BCH/RS failure), even if the CRC happens to pass.
  Alternative: CRC only. Why: a real receiver would also reject a frame its decoder flagged.
- 🔀 Payload is coded as consecutive k-bit chunks (last one zero-padded); `code_rate` is the
  *effective* rate 128 / coded bits (includes padding), e.g. Hamming(15,11) -> 128/180 = 0.711.
- Extra code `none` (uncoded baseline) is accepted besides the six required codes.
- All three programs accept every flag (irrelevant ones ignored), so the same command line
  works for all of them. Ports are named by link: `--channel-port` (9000), `--tollgate-port`
  (9001), `--feedback-port` (9002, reserved).
- Shutdown: vehicle sends END (twice) carrying the number of frames sent; the channel forwards it
  (twice); the tollgate prints the summary (lost = sent - received). Backstops: Ctrl-C (summary
  still printed) and `--idle-timeout-ms` (default 10 s, counted only after the first packet so
  the three-terminal demo can be started slowly).
- With `--delay-ms 0` the vehicle still sleeps 100 us between frames so loopback socket buffers
  never overflow (lost frames would distort the statistics).
- Decode latency = FEC decode + CRC check of one frame, measured with CLOCK_MONOTONIC.

## Phase 4

- One generic binary BCH implementation (`bch_init(m, primitive poly, t)`) builds g(x) as the LCM
  of minimal polynomials from cyclotomic cosets; BCH(15,7) and BCH(31,16) are two instances.
  Primitive polynomials: GF(16) x^4+x+1, GF(32) x^5+x^2+1.
- Decoder = syndromes -> Berlekamp-Massey -> Chien search (no Forney needed: binary errors).
  A pattern is reported as a detected failure when deg Lambda > t or the number of Chien roots
  differs from deg Lambda (it never outputs a non-codeword; tested exhaustively for weight 3).
- Syndromes and Chien search use the straightforward O(n * t) evaluation (no Horner/LFSR tricks)
  so the maths stays visible; latency is still only a few microseconds per frame.

## Phase 5

- The simulation loop lives in the library (`src/sim.c`, unit-tested); `bin/bench` only sweeps
  and writes CSV. Same seed for every point -> every code sees the same tags and noise process.
- 🔀 Swept parameter per model (CSV `param` column): bsc -> p in [0.0005, 0.1]; ge -> p_gb in
  [0.0005, 0.05] with the profile's p_bg/e_good/e_bad; burst -> burst length L in [1, 32] with a
  burst in *every* frame. Alternative: sweep the average BER for all models. Why: keeps each
  model's physical knob and shows the burst-length "cliff" of each code directly.
- `make bench` writes `results/bench.csv` (toll profile, exactly the required columns) and
  `results/bench_hospital.csv` (same sweep, hospital GE/burst parameters); `make plots` makes
  PNGs for both (suffix `_hospital`). PNGs are tracked as final plots, CSVs are ignored.
- The silent-wrong plot contrasts CRC-caught failures with silent errors on a log scale, because
  silent-wrong is 0 in all ~155k frames per code: CRC-32 makes it negligible (~2^-32 per
  failed frame). Zero counts are drawn as an upper bound (< 1/N).
- Plot colours: fixed colour per code from a CVD-validated categorical palette, so a code keeps
  its colour across all charts.

## Phase 6

- 🔀 Reed-Solomon uses the **whole 128-bit payload as one shortened codeword**: RS(16+2t, 16)
  over GF(256) (default t = 4 -> RS(24,16), rate 0.667), p(x) = x^8+x^4+x^3+x^2+1, roots
  alpha^1..alpha^2t. Alternative: several smaller RS codewords per frame with symbol
  interleaving. Why: simplest to explain, highest rate, and a single codeword already corrects
  every bit burst <= 8(t-1)+1 = 25 bits (tested exhaustively for every position).
- RS reuses the BCH Berlekamp-Massey routine (`bch_berlekamp_massey`, limit raised to t = 16) and
  adds Forney's formula for the error values. `--rs-t 1..16` travels in the packet header.
- 🔀 The interleaver is a generic **bit-level block interleaver** applied to the whole coded frame
  of ANY code (`--interleave D`, header field): rows written, columns sent. Alternative: symbol
  interleaver only for RS. Why: it is what rescues the bit-oriented codes (Hamming/BCH) in the
  hospital profile; RS does not need it (it would even spread a burst over more bytes).
  The "each block sees at most one burst error" guarantee holds when D divides the coded length.
- The benchmark adds interleaved variants `hamming74+il8` and `bch3116+il8` (drawn dashed in the
  base code's colour) and RS (`rs`, or `rs_t<N>` with `--rs-t N`).
