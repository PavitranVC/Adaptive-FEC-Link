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
