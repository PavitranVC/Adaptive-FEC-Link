# PROGRESS

## Status

| Phase | State |
|-------|-------|
| 0 Scaffolding | done - `make test` green |
| 1 Bits, CRC-32, Hamming(7,4)/(15,11), SECDED(8,4) | done - exhaustive tests green (gcc + clang) |
| 2 RNG, noise models (BSC/GE/burst), profiles | done - statistical tests green |
| 3 UDP pipeline, demo/compare scripts, integration test, docs/DEMO.md | done - `make test` green |
| 4 GF(2^m), BCH(15,7) t=2, BCH(31,16) t=3 (BM + Chien), wired into pipeline | done - exhaustive + 200k randomized tests green; `make compare` works |
| 5 bin/bench sweep -> results/bench.csv, tools/plot.py -> results/*.png | done - sim unit tests + bench/plot smoke test green |
| 6 Reed-Solomon GF(256) (shortened, t configurable, default 4) + block interleaver | done - burst-correction tests green, in pipeline + benchmark + plots |

## Next step

Week 2 (see PLAN.md).
