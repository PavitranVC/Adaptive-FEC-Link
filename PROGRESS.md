# PROGRESS

## Status

| Phase | State |
|-------|-------|
| 0 Scaffolding | done - `make test` green |
| 1 Bits, CRC-32, Hamming(7,4)/(15,11), SECDED(8,4) | done - exhaustive tests green (gcc + clang) |
| 2 RNG, noise models (BSC/GE/burst), profiles | done - statistical tests green |
| 3 UDP pipeline, demo/compare scripts, integration test, docs/DEMO.md | done - `make test` green; `make compare` needs bch157 (phase 4) |

## Next step

Phase 4 - GF(2^m) + BCH(15,7)/BCH(31,16) with Berlekamp-Massey and Chien search; register in src/fec.c.
