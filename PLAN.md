# PLAN

Each phase: tests first -> implementation -> `make test` green -> `git status` clean of build
outputs -> commit `phaseN: <summary>` -> push.

## Week 1

- [x] **Phase 0 - Scaffolding**: CLAUDE.md, README.md, PLAN.md, PROGRESS.md, DECISIONS.md,
      Makefile, .gitignore, requirements.txt, folder layout, test helper.
- [x] **Phase 1 - Bit utilities, CRC-32, Hamming(7,4), Hamming(15,11), SECDED(8,4)**.
      Tests: every single-bit error at every position corrected; 2-bit error makes plain Hamming
      miscorrect and CRC-32 catches it; SECDED detects 2-bit errors.
- [x] **Phase 2 - Channel noise models**: BSC(p), Gilbert-Elliott (p_gb, p_bg, e_good, e_bad),
      fixed burst of length L at random position. TOLL_PLAZA / HOSPITAL_IMAGING presets in one header.
      Tests: measured flip rates / burst lengths statistically close to parameters.
- [ ] **Phase 3 - UDP pipeline**: bin/vehicle, bin/channel, bin/tollgate with CLI flags; per-frame
      receiver log (seq, code, flipped, corrected, CRC, class, decode latency us); coloured output
      (--no-color); --delay-ms; make demo / demo-hospital / compare; integration test; docs/DEMO.md.
- [ ] **Phase 4 - Binary BCH**: BCH(15,7) t=2 and BCH(31,16) t=3, GF(2^m), syndromes,
      Berlekamp-Massey, Chien search. Exhaustive tests (weight <= t) for BCH(15,7), randomized
      for BCH(31,16). Wired into the pipeline.
- [ ] **Phase 5 - Benchmark**: bin/bench (in-process) sweeping code x model x parameter ->
      results/bench.csv (code, model, param, frames, frame_success_rate, detected_fail_rate,
      silent_wrong_rate, code_rate, mean_decode_us); tools/plot.py -> results/*.png.
- [ ] **Phase 6 - (if time) Reed-Solomon** over GF(256), shortened, configurable t (default 4),
      configurable block interleaver, burst-correction tests, pipeline + benchmark.

## Week 2 (do NOT build yet)

- [ ] Stop-and-Wait ARQ baseline (simulated RTT / timeout) - the "retransmission" strawman.
- [ ] Hybrid ARQ: FEC first, retransmit only when the CRC fails (uses feedback port 9002).
- [ ] Adaptive controller: receiver feedback steps the code level up/down
      (hamming74 -> bch157 -> bch3116 -> rs ...) with hysteresis.
- [ ] Live Python web dashboard (reads tollgate log / feedback stream).
- [ ] Deliberate failure-injection testing (malformed packets, lost END, port in use, bursts
      longer than the code can handle, header corruption experiment).
- [ ] Final report plots (both profiles, all codes, ARQ vs FEC vs HARQ vs adaptive).
- [ ] *Optional stretch*: LDPC with a bit-flipping decoder.
