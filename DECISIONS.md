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
