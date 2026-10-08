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
