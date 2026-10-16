# Project status

Current state of every phase, test status, honest findings, known issues and open problems.
Versions v0.1-v0.4 are defined in [ARCHITECTURE_HISTORY.md](ARCHITECTURE_HISTORY.md) (which also
holds the branch and tag map); design records DD-NN are in [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md);
the roadmap is [ROADMAP.md](ROADMAP.md).

**History in one paragraph.** v0.1 (phases 0-6, core FEC pipeline) and v0.2 (feedback, ARQ /
Hybrid ARQ, adaptive controller, dashboard) were each developed on their own branch and merged
into `main`. v0.3 (fair baselines, goodput, RTT sensitivity, then the hardening pass: failure
injection, sanitizers + fuzzing, multi-seed statistics, all-RS ladder experiment) and v0.4 (CI,
report, optional LDPC code, demo-guide fixes, documentation) were fast-forwarded into `main`
once `make test` passed. Two verification passes checked the published numbers against the
CSVs: pass A (design-decision numbers, v0.3) and pass B (report numbers, v0.4). Tag
`v0.2-week2` -> `fc5dc28` exists on GitHub (an earlier note said tag pushes had been refused with
HTTP 403; `git ls-remote --tags origin` shows it).

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
| v0.2-1 Ground-truth integrity check (DD-06, was #6) | **passed** - decoder never sees the sidecar; `src/receiver.c` + `tests/test_receiver.c` |
| v0.2-2 Stop-and-Wait ARQ + Hybrid ARQ, ACK/NAK on port 9002, `--fb-drop`, strategy bench | done |
| v0.2-3 Adaptive FEC controller, `--schedule`, level-over-time plot, `make demo-adaptive` | done |
| v0.2-4 Live dashboard (`--jsonl`, `tools/dashboard.py`, `make dashboard`) | done |
| v0.3-1 docs/DEMO.md = single demo guide (setup per OS, venv, troubleshooting, talking script) | done |
| v0.3-2 HARQ + RS t=4/t=8 baselines, goodput metric + plot, honest result tables | done - claim did NOT hold (see below) |
| v0.3-3 RTT sensitivity 5/20/50 ms, `results/rtt_sensitivity.png` | done |
| v0.3-4 Failure injection: `--corrupt-header P`, `tests/test_packet.c`, `tests/test_failure.sh`, header experiment | done - 3 real bugs fixed (see DESIGN_DECISIONS.md) |
| v0.3-5 `make sanitize` (ASan + UBSan, all tests) + `tests/test_fuzz.c` (packet/feedback/schedule parsers) | done - 1 real bug fixed (RS decoder read uninitialized memory on failure) |
| v0.3-6 `bench --strategies --seeds N` (mean + 95% CI, Student t), paired differences, `results/goodput_vs_success_ci.png` | done - see answers below |
| v0.3-7 NEW experiment: all-RS adaptive ladder (`--ladder rs`), pre-registered in DESIGN_DECISIONS.md | done - hypothesis HELD (goodput 0.584 vs 0.504 mixed / 0.491 RS t=8, success 99.999%); still below fixed HARQ + RS t=4 (0.613) |
| v0.4-1 GitHub Actions CI (ubuntu + macOS `make test`, ubuntu `make sanitize`) | done - green |
| v0.4-2 `docs/REPORT.md` (numbers sourced from the CSVs) + verification pass B | done |
| v0.4-3 (optional) LDPC(256,128) (3,6)-regular + Gallager bit flipping, `--code ldpc`, `results/bench_ldpc*.csv` + `*_ldpc*.png` | done - best for random errors, WORSE than RS on bursts / hospital, slowest decoder on average (see below) |
| v0.4-4 Demo guide checked on a real WSL run, `SHOWBITS`, measured timings | done |
| v0.4-5 Documentation: CONTRIBUTING, ALGORITHMS, DEVELOPMENT, DESIGN_DECISIONS (DD records), architecture history | done |

## CI

`.github/workflows/ci.yml`: `make` + `make test` on ubuntu-latest and macos-latest, plus `make sanitize`
on ubuntu. **First run (commit a5bc434, [run 38829733020](https://github.com/PavitranVC/Adaptive-FEC-Link/actions/runs/38829733020)): all 3 jobs green** - ubuntu build+test, macos-latest build+test, ubuntu `make sanitize`. All 15 CI runs up to `53c6109` concluded "success" (checked via the GitHub Actions API for ARCHITECTURE_HISTORY.md). matplotlib is pip-installed best-effort (plot smoke test skips if absent).

## LDPC (v0.4, optional stretch) - honest result

`--code ldpc` = (3,6)-regular LDPC(256,128), rate 0.500, seeded PEG-style H (no 4-cycles, girth 6,
full rank), systematic encoder by Gaussian elimination, Gallager bit flipping (max 50 iterations).
`tests/test_ldpc.c` (tests written first): zero syndrome for every codeword, every 1- and 2-bit error
corrected (exhaustive), success never returned for a non-codeword. Seed 42, 5000 frames/point:

- Random errors: best code at BSC p = 0.02 (0.987 vs BCH(31,16) 0.975, RS t=8 0.961, RS t=4 0.722);
  ties RS t=8 at p <= 0.01; slightly worse than BCH(31,16) at p >= 0.05.
- **Bursts: worse than Reed-Solomon.** Hospital GE p_gb = 0.005: 0.790 vs RS t=4 0.834, RS t=8 0.953;
  burst L = 16: 0.969 vs 1.000 (both RS); L = 24: 0.692 vs 1.000.
- **Decode time: worst on average** (10.5 us vs RS t=4 3.3, RS t=8 5.6, BCH(31,16) 6.9; toll file):
  ~3 us when it succeeds quickly, up to ~50 us when it fails (all 50 iterations).
- SILENT_WRONG 0. Not on the ladder; the ladder, the controller and all earlier CSVs are unchanged
  (checked against main: `bench.csv`, `bench_hospital.csv` identical, strategy CSV identical except
  timing-noise latency columns).

## Report (v0.4)

`docs/REPORT.md` written. All numbers come from the CSVs regenerated by `make bench` (+ `tools/header_experiment.sh 2000`),
or from STATUS.md / DESIGN_DECISIONS.md; none invented. Not measured (written as such / left out): real hardware, real
over-the-air channel, macOS/WSL results. Single-seed (seed 42) numbers are labelled as such; 20-seed means used for
strategy claims. Needs a human proofread before submission.

## Verification pass B (done)

Every number, figure path and file reference in `docs/REPORT.md` checked against `results/*.csv` (regenerated by `make bench`), README.md and DESIGN_DECISIONS.md: no mismatches; all 8 figure paths exist. One vague claim ("tie within the noise") replaced by the measured paired difference.

## Test status

`make test` = 24 unit-test binaries (v0.4: `test_ldpc`; v0.3: `test_packet` malformed/truncated/header-corruption,
`test_fuzz` 450k fuzzed parser inputs, `test_stats` confidence intervals) + `tests/test_integration.sh`
(real three-process UDP runs on ports 19100-19102: reproducibility, hospital profile, BCH, RS,
interleaver, compare, ARQ, HARQ with 30% injected feedback loss, adaptive on a toll->hospital->toll
schedule, adaptive with `--ladder rs`, JSON-lines log + dashboard HTTP server, bench / `--seeds` /
plot smoke tests) + `tests/test_failure.sh` (failure injection, ports 19200-19202) +
`tests/test_dashboard.py` (was never committed in v0.2 - `.gitignore` hid it, so `make test` failed
on a fresh clone; restored in v0.3).
**All green with gcc 13 and clang** on Ubuntu 24.04. **`make sanitize` (gcc, ASan + UBSan, every
unit and integration test): no findings** after one fix (RS decoder, see DESIGN_DECISIONS.md, v0.3 task 2).
`make sanitize CC=clang` needs clang's ASan runtime (`libclang-rt-18-dev`), not installed in the
build container. macOS is covered by CI only; WSL by one manual run of the demo guide. The code only
uses C11 + POSIX (sockets, poll, clock_gettime, nanosleep, sigaction) and POSIX `sh`/`awk`, but
please try `make test` on a Mac once.

## Key finding (v0.3 fair comparison, honest)

The adaptive controller does NOT give "the strongest code's reliability at clearly lower
overhead". On the toll -> hospital -> toll schedule (5000 frames): adaptive 99.94% correct,
goodput 0.505, p99 33 ms; always-strongest HARQ + RS t=8 100%, goodput 0.490, p99 33 ms (only ~3%
more overhead than adaptive); fixed HARQ + RS t=4 100%, goodput 0.612, p99 32 ms - better than
adaptive on every metric (single seed; **corrected by the 20-seed run below**: RS t=4 wins
goodput and success, adaptive wins retransmissions and p99). Reason: the ladder's lower rungs have lower code rates than RS t=4, so
stepping down saves nothing. What adaptive does show: it reacts within ~80 frames to the
hospital burst and never flaps (tests + `results/adaptive_level.png`). See DESIGN_DECISIONS.md.

## Multi-seed answers (v0.3, 20 seeds, scheduled channel)

- **Is adaptive's goodput advantage over HARQ + RS t=8 outside the confidence interval? Yes.**
  Paired difference +0.0133 +- 0.0024 (0.504 vs 0.491); it is small (+2.7%) and adaptive's
  success is 0.05 percentage points lower (99.95% vs 100.00%), also outside the CI.
- **Is HARQ + RS t=4 really better than adaptive on every metric? No.** It is clearly better on
  goodput (0.613 vs 0.504) and success (99.998% vs 99.95%), but adaptive has fewer
  retransmissions/frame (0.069 vs 0.088) and a lower p99 latency (33.2 ms vs 42 +- 5 ms; RS t=4's
  p99 is 32.4 ms in 11 seeds and 53.6 ms in 9 - seed 42 was a lucky one). The README table was
  updated to the 20-seed means because of that p99 and the "every metric" claim.

## All-RS ladder experiment (v0.3, NEW experiment, pre-registered)

Hypothesis held on the scheduled channel (20 seeds): goodput 0.584 +- 0.004 vs mixed adaptive
0.504 (paired +0.080 +- 0.005) and HARQ + RS t=8 0.491 (paired +0.093 +- 0.004); success 99.999%
vs 100.000% (within 0.5 pp). Not shown: it does not beat fixed HARQ + RS t=4 on goodput (0.584 vs
0.613; ties on toll alone, loses on hospital), only on retransmissions and p99 latency.
`results/ladder_rs_experiment.png`; numbers and criterion in DESIGN_DECISIONS.md (DD-24).

## Known issues / limitations

- Nothing known broken.
- `make bench` takes about 1-2 minutes, host-dependent (two 20-seed runs + the two LDPC
  comparison sweeps; ~1:50 and 108 s measured in the build container, ~1 min on a WSL laptop).
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
  failure rate and the probe back-off; see DESIGN_DECISIONS.md).

## Open problems

- **Header corruption -> misrouting** (v0.3 failure injection, not fixed: needs a wire-format change).
  Reproduce: `sh tools/header_experiment.sh 2000` -> `results/header_corruption.csv`. A flipped
  seq bit files a frame under another seq; the tollgate then counts a phantom frame and, with
  HARQ, rejects the real owner of that seq as a "late copy" until the vehicle gives up; the
  summary over-reports CORRECT (2003 of 2000 at P = 0.2). Fix: protect the header with its own
  CRC-16 (new header version) and drop on mismatch.

## Next steps

1. Local verification: `make test` on the team's own machines (macOS / WSL), then a rehearsal
   with `docs/DEMO.md`. LDPC is optional material - mention it in the talk only as "tried, loses
   to RS on bursts" (REPORT 7.6).
2. Final report: build it around `results/goodput_vs_success_ci.png` (20-seed CIs),
   `results/strategy_comparison.png`, `results/adaptive_level.png`, `results/rtt_sensitivity.png`,
   `results/ladder_rs_experiment.png` (NEW experiment, label it so) and the honest findings above.
   Use the 20-seed numbers (README table), not the single-seed ones. `docs/REPORT.md` still needs
   a human proofread.
3. Team decision: keep `--ladder mixed` as default for the demo (everything is documented with it),
   or switch the demo to `rs`. Either way the RS ladder is reported as a new experiment.
4. Optional: protect the header with a CRC-16 (fixes the misrouting found by `--corrupt-header`).

Done: verification pass A (design-decision numbers against `results/*.csv`; two numbers corrected)
and verification pass B (report numbers; no mismatches).

## Decisions to review

All direction-changing choices are design records in [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md)
(the "was" column keeps the old numbers #1-#26):

| record | was | decision |
|---|---|---|
| DD-01 | #1 | Frame header is never corrupted (assignment default). |
| DD-02 | #2 | Gilbert-Elliott state persists across frames, starts in the stationary distribution. |
| DD-03 | #3 | Burst model = classic burst of length L (first/last bit flipped, inner bits w.p. 0.5), one burst per frame with probability p_frame. |
| DD-04 | #4 | `--p` meaning per model: bsc -> p, ge -> p_gb, burst -> burst probability per frame. |
| DD-05 | #5 | Profile numbers (TOLL_PLAZA / HOSPITAL_IMAGING) are modelling assumptions. |
| DD-06 | #6 | Ground truth travels as an evaluation-only sidecar appended by the channel. |
| DD-07 | #7 | DETECTED_FAIL = CRC fail OR decoder-flagged failure. |
| DD-08 | #8 | Payload coded as consecutive k-bit chunks with zero padding; code_rate is the effective rate. |
| DD-09 | #9 | Bench sweep parameters per model (burst sweeps L with a burst in every frame). |
| DD-10 | #10 | Reed-Solomon = one shortened RS(16+2t, 16) codeword per frame. |
| DD-11 | #11 | Interleaver = generic bit-level block interleaver for any code. |
| DD-12 | #17 | ARQ sends uncoded frames (CRC only); HARQ is type I (no soft combining). |
| DD-13 | #18 | Simulated RTT split rtt/2 + rtt/2 between vehicle and tollgate. RTT itself is a modelling assumption (5/20/50 ms sensitivity run: rankings unchanged). |
| DD-14 | #18 | Bench latency = delivery latency on a simulated clock at 160 kbit/s. |
| DD-15 | #12 | Ladder top = RS t=8 instead of "RS + interleaving" (bit interleaving measurably hurts RS). |
| DD-16 | #13 | The vehicle runs the controller; the tollgate only reports window statistics. |
| DD-17 | #14 | Step up on measured failure rate; step down only if the binomial model predicts the cheaper code copes at the measured BER^. |
| DD-18 | #15 | Hysteresis: up 0.20 / down 0.05, one decision per W=32 fresh frames, probe back-off. |
| DD-19 | #16 | `--schedule` counts transmissions (air time), not frames. |
| DD-20 | #19 | Dashboard uses the Python standard library only (no Flask). |
| DD-21 | #20 | Goodput counts the 128-bit payload (ID + CRC) as useful, all coded bits as cost. |
| DD-22 | #21 | Header corruption = 1-4 random header bit flips per hit packet, END exempt (failure injection). |
| DD-23 | #22 | 95% CI over seeds from Student's t (not bootstrap); strategies compared by paired differences. |
| DD-24 | #23 | `--ladder rs` (all-RS ladder) is a separate, pre-registered experiment; default stays `mixed`. |
| DD-25..27 | #24 | LDPC = (3,6)-regular LDPC(256,128), PEG-style seeded H (no 4-cycles), Gaussian-elimination encoder. |
| DD-28 | #25 | Decoder = parallel Gallager bit flipping, hard decision, max 50 iterations (not tuned). |
| DD-29 | #26 | LDPC is benchmarked in a separate `--compare-codes` sweep (default sweep unchanged); RS t=8 added to that comparison as the same-rate baseline. |

## Branches and tags

Working branch names are being replaced by technical names (`release/vX.Y-<topic>`,
`feature/<topic>`, `docs/<topic>`, `chore/<topic>`) and versions get annotated tags
`v0.1.0`..`v0.4.0`; the full old -> new map with commit ranges is in
[ARCHITECTURE_HISTORY.md](ARCHITECTURE_HISTORY.md#branch-and-tag-map). Status: planned (updated
when the renames are done).

## Demo guide fixes (v0.4)

Changes after a real WSL run of docs/DEMO.md: WSL note and the real last line of `make test`; talking
script rewritten (228 spoken words, all numbers from results/bench_strategy.csv, bench_hospital.csv,
`make compare`; ARQ/HARQ row now shows `strategy_comparison.png` instead of the 20-frame demos);
`SHOWBITS` variable (`tools/demo.sh` env + Makefile, default 1; `make demo-arq`/`demo-harq` default 0) with an
integration check; `make demo-harq` now uses `--profile hospital` so retransmissions are visible
(seed 42: 42 retransmissions, 4 frames given up with Hamming(7,4)); DEMO.md section 5 lists the real `make bench`
outputs and the ~26 plots; dashboard section states 120 ms/frame (first estimate ~36 s + 3 s; the measured WSL run gives ~33 s of demo after a 3 s start) and `/api/state`; plot.py
mentions the venv. Open: none. Not verified: the venv message was only checked without matplotlib installed.
