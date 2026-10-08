# PROGRESS

Branch: `claude/bold-gauss-xza289`. Every phase was committed and pushed (all pushes succeeded).

## Status

| Phase | State |
|-------|-------|
| 0 Scaffolding | done |
| 1 Bits, CRC-32, Hamming(7,4)/(15,11), SECDED(8,4) | done - exhaustive tests |
| 2 RNG, noise models (BSC / Gilbert-Elliott / burst), profiles | done - statistical tests |
| 3 UDP pipeline, demo/compare scripts, integration test, docs/DEMO.md | done |
| 4 GF(2^m), BCH(15,7) t=2, BCH(31,16) t=3 (Berlekamp-Massey + Chien) | done - exhaustive (weight <= t) + 200k randomized tests |
| 5 bin/bench sweep -> results/bench.csv, tools/plot.py -> results/*.png | done |
| 6 Reed-Solomon GF(256) (shortened, `--rs-t`, default 4) + block interleaver (`--interleave`) | done - burst-correction tests |

## Test status

`make test` = 15 unit-test binaries + `tests/test_integration.sh` (real three-process UDP runs on
ports 19100-19102, reproducibility check, hospital profile, BCH, RS, interleaver, compare script,
bench + plot smoke test). **All green with gcc 13 and clang** on Ubuntu 24.04.
Not run on macOS/WSL in this session (no machine available) - the code only uses C11 + POSIX
(sockets, poll, clock_gettime, nanosleep, sigaction) and POSIX `sh`/`awk`, but please try
`make test` on a Mac once.

## Known issues / limitations

- Nothing known broken.
- `results/*.png` latency plots change slightly on every `make bench` (timing noise); commit
  them only when you want to update the "final" plots.
- The tollgate ignores duplicate frames by `seq mod 65536`, fine for runs < 65536 frames.
- Unit-test output of `test_cli` prints 5 expected error messages (bad-flag tests).

## Exact next step

Week 2, first item: Stop-and-Wait ARQ baseline. Suggested start: add a `PKT_ACK`/`PKT_NAK`
type to `include/packet.h` (+ tests in `tests/test_frame.c`), let the tollgate send it to the
vehicle on `--feedback-port` 9002, and give the vehicle a `--arq {none,sw,hybrid}` flag with a
simulated RTT / timeout. Then Hybrid ARQ (retransmit only on DETECTED_FAIL) and the adaptive
controller (step hamming74 -> bch157 -> bch3116 -> rs with hysteresis on the feedback stream).

## 🔀 Decisions to review (details in DECISIONS.md)

1. Frame header is never corrupted (assignment default).
2. Gilbert-Elliott state persists across frames, starts in the stationary distribution.
3. Burst model = classic burst of length L (first/last bit flipped, inner bits w.p. 0.5),
   one burst per frame with probability p_frame.
4. `--p` meaning per model: bsc -> p, ge -> p_gb, burst -> burst probability per frame.
5. Profile numbers (TOLL_PLAZA / HOSPITAL_IMAGING) are modelling assumptions.
6. Ground truth travels as an evaluation-only sidecar appended by the channel.
7. DETECTED_FAIL = CRC fail OR decoder-flagged failure.
8. Payload coded as consecutive k-bit chunks with zero padding; code_rate is the effective rate.
9. Bench sweep parameters per model (burst sweeps L with a burst in every frame).
10. Reed-Solomon = one shortened RS(16+2t, 16) codeword per frame.
11. Interleaver = generic bit-level block interleaver for any code.
