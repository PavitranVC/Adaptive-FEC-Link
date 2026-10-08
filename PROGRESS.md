# PROGRESS

Week 1 on `claude/bold-gauss-xza289` (merged into `main`). Week 2 on `week2`, merged into `main`
(tag `v0.2-week2` exists only locally: the session proxy refused tag pushes with HTTP 403).
Final polish on `final-polish`, fast-forwarded into `main`.

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
| W2-1 Ground-truth integrity check (decision #6) | **passed** - decoder never sees the sidecar; `src/receiver.c` + `tests/test_receiver.c` |
| W2-2 Stop-and-Wait ARQ + Hybrid ARQ, ACK/NAK on port 9002, `--fb-drop`, strategy bench | done |
| W2-3 Adaptive FEC controller, `--schedule`, level-over-time plot, `make demo-adaptive` | done |
| W2-4 Live dashboard (`--jsonl`, `tools/dashboard.py`, `make dashboard`) | done |
| Polish 1 docs/DEMO.md = single demo guide (setup per OS, venv, troubleshooting, talking script) | done |
| Polish 2 HARQ + RS t=4/t=8 baselines, goodput metric + plot, honest result tables | done - claim did NOT hold (see below) |
| Polish 3 RTT sensitivity 5/20/50 ms, `results/rtt_sensitivity.png` | done |

## Test status

`make test` = 20 unit-test binaries + `tests/test_integration.sh` (real three-process UDP runs on
ports 19100-19102: reproducibility, hospital profile, BCH, RS, interleaver, compare, ARQ, HARQ
with 30% injected feedback loss, adaptive on a toll->hospital->toll schedule, JSON-lines log +
dashboard HTTP server, bench + plot smoke tests) + `tests/test_dashboard.py`.
**All green with gcc 13 and clang** on Ubuntu 24.04.
Not run on macOS/WSL in this session (no machine available) - the code only uses C11 + POSIX
(sockets, poll, clock_gettime, nanosleep, sigaction) and POSIX `sh`/`awk`, but please try
`make test` on a Mac once.

## Key finding (final polish, honest)

The adaptive controller does NOT give "the strongest code's reliability at clearly lower
overhead". On the toll -> hospital -> toll schedule (5000 frames): adaptive 99.94% correct,
goodput 0.505, p99 33 ms; always-strongest HARQ + RS t=8 100%, goodput 0.490, p99 33 ms (only ~3%
more overhead than adaptive); fixed HARQ + RS t=4 100%, goodput 0.612, p99 32 ms - better than
adaptive on every metric. Reason: the ladder's lower rungs have lower code rates than RS t=4, so
stepping down saves nothing. What adaptive does show: it reacts within ~80 frames to the
hospital burst and never flaps (tests + `results/adaptive_level.png`). See DECISIONS.md.

## Known issues / limitations

- Nothing known broken.
- `results/*.png` latency plots change slightly on every `make bench` (timing noise); commit
  them only when you want to update the "final" plots.
- The tollgate ignores duplicate frames by `seq mod 65536`, fine for runs < 65536 frames.
- Unit-test output of `test_cli` prints 6 expected error messages (bad-flag tests).
- The link simulator adds the REAL measured decode time to its simulated clock, so latency
  columns of `bench_strategy.csv` vary by microseconds between runs (success/retransmissions are
  bit-reproducible).
- The adaptive flags (`--window`, thresholds) must be given to vehicle AND tollgate (the tollgate
  owns the window); `tools/demo.sh` does that automatically.
- BER^ ignores failed frames, so it under-estimates on bursty channels (mitigated by the measured
  failure rate and the probe back-off; see DECISIONS.md).

## Exact next step

1. Failure-injection testing (PLAN.md): add `--corrupt-header P` to `bin/channel`, malformed and
   truncated packets, lost END, port already in use; each with a test that the receiver logs and
   drops instead of crashing.
2. Final report: build it around `results/goodput_vs_success.png`, `results/strategy_comparison.png`,
   `results/adaptive_level.png`, `results/rtt_sensitivity.png` and the honest finding above.
3. Optional experiment (decide as a team, report it as a NEW experiment, not a fix): an all-RS
   ladder (t = 1..8) where lower rungs really have higher code rates.

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

### New in week 2

12. Ladder top = RS t=8 instead of "RS + interleaving" (bit interleaving measurably hurts RS).
13. The vehicle runs the controller; the tollgate only reports window statistics.
14. Step up on measured failure rate; step down only if the binomial model predicts the cheaper
    code copes at the measured BER^.
15. Hysteresis: up 0.20 / down 0.05, one decision per W=32 fresh frames, probe back-off.
16. `--schedule` counts transmissions (air time), not frames.
17. ARQ sends uncoded frames (CRC only); HARQ is type I (no soft combining).
18. Simulated RTT split rtt/2 + rtt/2 between vehicle and tollgate; bench latency = delivery
    latency on a simulated clock at 160 kbit/s. RTT itself is a modelling assumption (5/20/50 ms
    sensitivity run: rankings unchanged).
20. Goodput counts the 128-bit payload (ID + CRC) as useful, all coded bits as cost.
19. Dashboard uses the Python standard library only (no Flask).
