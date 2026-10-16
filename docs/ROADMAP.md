# Roadmap

Each phase: tests first -> implementation -> `make test` green -> `git status` clean of build
outputs -> commit -> push. Versions as defined in [ARCHITECTURE_HISTORY.md](ARCHITECTURE_HISTORY.md);
current state in [STATUS.md](STATUS.md).

## v0.1 - core FEC pipeline (phases 0-6)

- [x] **Phase 0 - Scaffolding**: contributor guide (now `CONTRIBUTING.md`), README.md, roadmap,
      status and design-decision documents, Makefile, .gitignore, requirements.txt, folder
      layout, test helper.
- [x] **Phase 1 - Bit utilities, CRC-32, Hamming(7,4), Hamming(15,11), SECDED(8,4)**.
      Tests: every single-bit error at every position corrected; 2-bit error makes plain Hamming
      miscorrect and CRC-32 catches it; SECDED detects 2-bit errors.
- [x] **Phase 2 - Channel noise models**: BSC(p), Gilbert-Elliott (p_gb, p_bg, e_good, e_bad),
      fixed burst of length L at random position. TOLL_PLAZA / HOSPITAL_IMAGING presets in one header.
      Tests: measured flip rates / burst lengths statistically close to parameters.
- [x] **Phase 3 - UDP pipeline**: bin/vehicle, bin/channel, bin/tollgate with CLI flags; per-frame
      receiver log (seq, code, flipped, corrected, CRC, class, decode latency us); coloured output
      (--no-color); --delay-ms; make demo / demo-hospital / compare; integration test; docs/DEMO.md.
- [x] **Phase 4 - Binary BCH**: BCH(15,7) t=2 and BCH(31,16) t=3, GF(2^m), syndromes,
      Berlekamp-Massey, Chien search. Exhaustive tests (weight <= t) for BCH(15,7), randomized
      for BCH(31,16). Wired into the pipeline.
- [x] **Phase 5 - Benchmark**: bin/bench (in-process) sweeping code x model x parameter ->
      results/bench.csv (code, model, param, frames, frame_success_rate, detected_fail_rate,
      silent_wrong_rate, code_rate, mean_decode_us); tools/plot.py -> results/*.png.
- [x] **Phase 6 - (if time) Reed-Solomon** over GF(256), shortened, configurable t (default 4),
      configurable block interleaver, burst-correction tests, pipeline + benchmark.

## v0.2 - feedback, ARQ, adaptive FEC, dashboard

- [x] Ground-truth integrity check (DD-06): receiver module + truth-stripping test.
- [x] Stop-and-Wait ARQ baseline (simulated RTT / timeout) - the "retransmission" strawman.
- [x] Hybrid ARQ: FEC first, retransmit only when the CRC fails (uses feedback port 9002).
- [x] Adaptive controller: receiver feedback steps the code level up/down
      (hamming74 -> bch157 -> bch3116 -> rs ...) with hysteresis.
- [x] Live Python web dashboard (tollgate --jsonl + tools/dashboard.py, `make dashboard`).
- [x] Deliberate failure-injection testing (malformed packets, lost END, port in use, bursts
      longer than the code can handle, header corruption experiment) - delivered in v0.3.
- [x] Final report plots (both profiles, all codes, ARQ vs FEC vs HARQ vs adaptive).
- [x] *Optional stretch*: LDPC with a bit-flipping decoder - delivered in v0.4.

## v0.3 - evaluation: fair baselines, goodput, RTT, hardening

- [x] Single demo guide (`docs/DEMO.md`).
- [x] HARQ + RS t=4 / t=8 baselines, goodput metric, honest result tables.
- [x] RTT sensitivity 5 / 20 / 50 ms.
- [x] Task 1: failure injection (`--corrupt-header`, malformed/truncated/duplicate packets, lost END,
      port in use, over-long bursts, 100% feedback loss) + header-corruption experiment.
- [x] Task 2: `make sanitize` (ASan + UBSan, all tests) + seeded fuzz test of the parsers.
- [x] Task 3: `bench --strategies --seeds N` (mean + 95% CI), paired differences, CI plot.
- [x] Task 4: all-RS ladder experiment (`--ladder rs`), pre-registered, reported.
- [x] Verification pass A: design-decision numbers checked against `results/*.csv`.

## v0.4 - CI, report, LDPC, documentation

- [x] Task 5: GitHub Actions CI (`.github/workflows/ci.yml`) - ubuntu + macOS `make test`, ubuntu
      `make sanitize`; all runs green.
- [x] Task 6: `docs/REPORT.md` draft (numbers from results/*.csv, STATUS, DESIGN_DECISIONS) +
      verification pass B.
- [x] Optional LDPC (below).
- [x] Demo guide fixes after a real WSL run; measured timings.
- [x] Professional documentation: `CONTRIBUTING.md`, `docs/ALGORITHMS.md`, `docs/DEVELOPMENT.md`,
      design records DD-NN, architecture history, technical branch names and version tags.
- [ ] Next (local): `make test` on the team's own macOS/WSL machines and rehearsal.

### Optional stretch: LDPC

- [x] Step 1: (3,6)-regular LDPC(256,128): seeded PEG-style H (no 4-cycles, full rank), systematic
      encoder by Gaussian elimination. Tests first (`tests/test_ldpc.c`).
- [x] Step 2: Gallager bit-flipping decoder (max 50 iterations, failure if syndrome != 0). Tests:
      zero syndrome for every codeword, every single and double error corrected (exhaustive),
      success never returned for a non-codeword.
- [x] Step 3: `--code ldpc` in the FEC registry, `bin/bench --compare-codes`, `results/bench_ldpc*.csv`,
      `results/*_ldpc*.png`; default sweep and every existing result unchanged.
- [x] Step 4: honest comparison vs BCH/RS (DESIGN_DECISIONS.md "v0.4 - LDPC", README, REPORT 7.6).

## Future work

See [REPORT.md](REPORT.md) section 10: header CRC-16, soft-combining HARQ, soft-decision LDPC,
a controller that can pick the best fixed code, real channel traces / hardware in the loop.
