# Architecture history

How the architecture of *Adaptive Multi-Error FEC Link Layer for High-Interference Environments*
changed from the first commit to the final `main`, why each change was made, and what failed.

**Sources.** Everything below comes from `git log` / `git show` / `git ls-tree` on `main` and the
remote branches, from `DESIGN_DECISIONS.md`, `STATUS.md`, `ROADMAP.md`, `README.md`, `docs/REPORT.md`,
`docs/DEMO.md`, and from the `results/*.csv` files regenerated with `make bench` on `main`
(`53c6109`) while writing this document (the CSVs are git-ignored, so they had to be regenerated).
A few checks were re-run by hand for this document; they are marked **(re-run for this document)**.
A number that could not be sourced is written "not measured".

**Version boundaries (confirmed from git).** The history is one linear chain of 37 commits on
`main`; every feature branch was fast-forwarded into it. Branch heads: `claude/bold-gauss-xza289`
= `aa2aaac`, `week2` = `fc5dc28` (= tag `v0.2-week2`), `final-polish` = `c4d7bf4`, `overnight-a`
= `bde2409`, `gate-a` = `754e3e9`, `overnight-b` = `ecd770f`, `gate-b` = `9933409`, `overnight-c`
= `539acca`, `docs-fix` = `b9f8436`, `docs-timings` = `53c6109` = `main`.

| version | git range (oldest..newest) | commits | dates | branches |
|---|---|---|---|---|
| v0.1 | `18c8a87`..`aa2aaac` | 9 | 2026-10-08 | `claude/bold-gauss-xza289` |
| v0.2 | `04a7423`..`fc5dc28` | 6 | 2026-10-08 | `week2`, tag `v0.2-week2` |
| v0.3 | `a6c204e`..`754e3e9` | 12 | 2026-10-08 .. 2026-10-16 | `final-polish`, `overnight-a`, `gate-a` |
| v0.4 | `a5bc434`..`53c6109` | 10 | 2026-10-16 | `overnight-b`, `gate-b`, `overnight-c`, `docs-fix`, `docs-timings` |

Adjustment to the brief: `gate-a` (`754e3e9`) is counted in **v0.3**, because it only corrects
numbers of the overnight-A results; `gate-b` (`9933409`) is counted in v0.4 because it checks
`docs/REPORT.md`, which is a v0.4 file.

Contents: [v0.1](#v01---week-1-phases-0-6) · [v0.2](#v02---week-2-feedback-arq-harq-adaptive-dashboard) ·
[v0.3](#v03---final-polish--overnight-a-fair-baselines-statistics-failure-injection) ·
[v0.4](#v04---overnight-bc-and-docs-ci-report-ldpc-demo-fixes) ·
[A. At a glance](#a-architecture-evolution-at-a-glance) · [B. Results timeline](#b-results-timeline) ·
[C. Lessons](#c-lessons-learned) · [D. Doc issues](#d-doc-issues) · [E. Reproduce](#e-how-to-reproduce-each-version)

---

## v0.1 - Week 1, phases 0-6

### 1. Summary

One-way FEC link: `bin/vehicle` encodes the 96-bit tag ID + CRC-32 with one of six codes (plus an
uncoded baseline), `bin/channel` flips bits (BSC, Gilbert-Elliott or burst model, two profiles)
and appends a ground-truth sidecar, `bin/tollgate` decodes and classifies. All coding logic lives
in one library (`build/libfeclink.a`) shared with an in-process benchmark (`bin/bench`).
Git range `18c8a87`..`aa2aaac` (phase0 `18c8a87`, phase1 `7ab039a`, phase2 `612e846`,
phase3 `c9bc2c7`, phase4 `bbbd2ed`, phase5 `175f75e`, phase6 `d8f026a`, docs `c9cd2a3`, `aa2aaac`).

### 2. Architecture diagram

Everything is new in v0.1. Port 9002 exists only as a reserved flag (`--feedback-port`); nothing
is sent on it yet.

```mermaid
flowchart LR
    V["bin/vehicle (new)<br/>encode ID96 + CRC32"] -- "UDP 9000 DATA / END" --> C["bin/channel (new)<br/>noise + ground-truth sidecar"]
    C -- "UDP 9001 DATA + sidecar" --> T["bin/tollgate (new)<br/>decode, CRC, classify, summary"]
    T -. "UDP 9002 reserved, unused" .-> V
    subgraph LIB["build/libfeclink.a (new)"]
        CODES["hamming, secded, bch, rs, gf (new)"]
        CORE["bits, crc32, fec, frame, packet, interleave (new)"]
        NOISE["rng, noise, profiles (new)"]
        SIM["sim, cli, netutil, term (new)"]
    end
    B["bin/bench (new)<br/>in-process sweep"] --> SIM
    B -- "CSV" --> R["results/bench.csv<br/>results/bench_hospital.csv"]
    R --> P["tools/plot.py (new)<br/>results/*.png"]
    V --> CORE
    C --> NOISE
    T --> CODES
```

ASCII fallback:

```
 bin/vehicle (new) --UDP 9000--> bin/channel (new) --UDP 9001--> bin/tollgate (new)
  encode ID96+CRC32               noise (bsc/ge/burst)            decode, CRC-32 check,
  FEC code + interleaver          + ground-truth sidecar          classify, summary
        ^                                                                |
        +------------------ UDP 9002 (reserved, unused) ----------------+

 build/libfeclink.a (new): bits crc32 hamming secded gf bch rs interleave fec frame packet
                           rng noise profiles sim cli netutil term version
 bin/bench (new) --> sim (same library, no sockets) --> results/bench.csv, bench_hospital.csv
                                                    --> tools/plot.py --> results/*.png (10 PNGs)
```

### 3. What changed

| component | change | files / modules | commit |
|---|---|---|---|
| scaffolding | Makefile, docs, home-made test helper | `Makefile`, `tests/testlib.h`, `CONTRIBUTING.md`, `ROADMAP.md`, `DESIGN_DECISIONS.md` | `18c8a87` |
| coding core | bits one-per-byte, CRC-32 (bitwise IEEE 802.3), Hamming(7,4)/(15,11), SECDED(8,4) | `src/bits.c`, `crc32.c`, `hamming.c`, `secded.c` | `7ab039a` |
| channel model | xoshiro256** RNG, BSC / Gilbert-Elliott / burst, TOLL_PLAZA and HOSPITAL_IMAGING | `src/rng.c`, `noise.c`, `profiles.c` | `612e846` |
| processes | three UDP programs, wire format v1 (14-byte header), sidecar, CLI, demo/compare scripts | `src/apps/*.c`, `src/packet.c`, `frame.c`, `fec.c`, `cli.c`, `netutil.c`, `tools/demo.sh`, `compare.sh`, `docs/DEMO.md` | `c9bc2c7` |
| codes | GF(2^m), BCH(15,7) t=2, BCH(31,16) t=3 (Berlekamp-Massey + Chien) | `src/gf.c`, `bch.c` | `bbbd2ed` |
| evaluation | in-process benchmark, CSV, matplotlib plots | `src/sim.c`, `tools/bench.c`, `tools/plot.py` | `175f75e` |
| codes | shortened RS(16+2t,16) over GF(256), generic bit block interleaver | `src/rs.c`, `interleave.c` | `d8f026a` |
| docs | results table in README, final PROGRESS | `README.md`, `STATUS.md` | `c9cd2a3`, `aa2aaac` |

### 4. Why

| change | problem it solved / question it answered | evidence |
|---|---|---|
| library + three thin programs | benchmark and UDP pipeline must run exactly the same code | DESIGN_DECISIONS.md "Phase 0" |
| CRC-32 on top of every code | a miscorrection must never bill the wrong car | SILENT_WRONG 0 of 1,395,000 frames in `bench.csv` and 0 of 1,395,000 in `bench_hospital.csv` |
| ground-truth sidecar (decision #6) | live CORRECT / DETECTED_FAIL / SILENT_WRONG classification in the demo | DESIGN_DECISIONS.md "Phase 3" |
| Gilbert-Elliott state kept across frames (#2) | real interference does not know frame boundaries | DESIGN_DECISIONS.md "Phase 2" |
| BCH(31,16) | stronger random-error correction than Hamming | toll BSC p=0.01: 0.9986 vs Hamming(7,4) 0.9354 (`bench.csv`) |
| Reed-Solomon (byte symbols) | bit-level codes collapse on hospital bursts | hospital GE p_gb=0.005: RS t=4 0.8336 vs BCH(31,16) 0.4936, Hamming(7,4) 0.4170 (`bench_hospital.csv`) |
| bit interleaver (#11) | rescue Hamming/BCH against bursts | burst L=16: BCH(31,16) 0.0126 -> BCH(31,16)+il8 1.0000 (`bench.csv`) |
| in-process benchmark (`src/sim.c`) | sweep every code over 31 noise points x 5000 frames with exactly the pipeline's library code, same seed for every point | DESIGN_DECISIONS.md "Phase 5"; 279 rows x 5000 frames in `bench.csv` |

### 5. Wire format / interface changes

| item | v0.1 |
|---|---|
| DATA/END header | 14 bytes, `PACKET_VERSION 1`: magic "FL", ver, type, code, rs_t, interleave, flags, seq (4), nbits (2) |
| payload | `FEC(ID96 || CRC32)`, bits packed MSB-first, the only corrupted part |
| sidecar | flips (2), profile, model, true tag (12), clean coded bits |
| CLI flags | `--code --profile --model --p --seed --count --delay-ms --burst-len --rs-t --interleave --channel-port --tollgate-port --feedback-port --host --idle-timeout-ms --log --summary-file --show-bits --quiet --color/--no-color --frames --out --help` |
| codes (`--code`) | `none hamming74 hamming1511 secded84 bch157 bch3116 rs` (7 incl. uncoded) |
| make targets | `all test unit-test integration-test demo demo-hospital compare bench plots clean` |

### 6. Test and verification

| test | what it checks | result |
|---|---|---|
| 15 unit-test binaries, 91 test cases (`tests/test_*.c` at `aa2aaac`) | exhaustive single-error Hamming, SECDED double-error detection, BCH weight <= t exhaustive + 200k randomized, RS burst correction, noise statistics | pass |
| `tests/test_integration.sh` | real three-process UDP runs, reproducibility, hospital, BCH, RS, interleaver, compare, bench + plot smoke tests | pass |
| fresh checkout of `aa2aaac`: `make` + `make test` | **(re-run for this document)** | `make` exit 0, `make test` exit 0, "Integration test passed." (plot smoke test skipped: no matplotlib) |

### 7. What failed / what we got wrong

1. **Bit-level codes barely beat "uncoded" on the hospital channel.**
   Expected: any FEC code clearly helps. Actual (hospital GE p_gb=0.005, `bench_hospital.csv`):
   uncoded 0.3952, Hamming(7,4) 0.4170, BCH(15,7) 0.3912, SECDED(8,4) 0.3654 - two codes are
   *worse* than no code. Root cause: Hamming/BCH correct 1-3 bits per block; a 12.5-bit mean burst
   puts many errors in one block. Handled: RS and the interleaver were added in phase 6 (`d8f026a`),
   RS t=4 reaches 0.8336.
2. **The bigger Hamming and SECDED were worse than Hamming(7,4).** Toll BSC p=0.01 (`bench.csv`):
   Hamming(15,11) 0.8956 and SECDED(8,4) 0.9174 vs Hamming(7,4) 0.9354. Root cause: a longer block
   collects more errors per block; SECDED only *detects* the second error, so frame success does
   not improve. Reported as a result, nothing changed.
3. **Bursts longer than the code's design still break everything.** Burst L=16 in every frame:
   Hamming(7,4) 0.0014, BCH(31,16) 0.0126 (`bench.csv`). Expected (it is the point of the burst
   sweep); handled by RS / interleaving. The interleaver's "one burst error per block" guarantee
   only holds when the depth divides the coded length (DESIGN_DECISIONS.md "Phase 6") - left as documented.

### 8. Decisions still open / limitations

- The header is never corrupted (decision #1) - tested only later (v0.3).
- Noise parameters of both profiles are modelling assumptions (decision #5), not measurements.
- No feedback path: a DETECTED_FAIL frame is simply lost (no retransmission).
- RS uses one shortened codeword per frame (decision #10); a symbol interleaver for RS was not built.
- Decode latency is measured on the simulation host only.

---

## v0.2 - Week 2: feedback, ARQ, HARQ, adaptive, dashboard

### 1. Summary

The link became two-way. The tollgate answers every frame with ACK/NAK plus sliding-window
statistics on UDP 9002; the vehicle runs a stop-and-wait state machine (ARQ, Hybrid ARQ) and an
adaptive controller that climbs a six-rung code ladder. The decoder was moved into its own module
so it structurally cannot read the ground truth. A link simulator runs the same protocol in-process
on a simulated clock, and a stdlib-only Python dashboard shows a run live.
Git range `04a7423`..`fc5dc28` (task1 `04a7423`, task2 `5c3040a`, task3 `04e8807`, task4 `abcfd0b`,
docs `fad9808`, `fc5dc28`); tag `v0.2-week2` -> `fc5dc28`.

### 2. Architecture diagram

```mermaid
flowchart LR
    subgraph VEH["bin/vehicle"]
        SND["sender: stop-and-wait (new)"]
        ADA["adapt: ladder controller (new)"]
    end
    subgraph CHN["bin/channel"]
        NZ["noise + sidecar"]
        SCH["schedule: toll/hospital switch (new)"]
    end
    subgraph TG["bin/tollgate"]
        RX["receiver: rx_decode / rx_classify (new)"]
        WIN["window: last W frames (new)"]
    end
    VEH -- "UDP 9000 DATA, header v2" --> CHN
    CHN -- "UDP 9001 DATA + sidecar" --> TG
    TG -. "UDP 9002 ACK/NAK + window stats (new)" .-> VEH
    TG -- "--jsonl live log (new)" --> J["results/tmp/live.jsonl"]
    J --> D["tools/dashboard.py (new)<br/>http://127.0.0.1:8050"]
    LS["linksim: simulated clock (new)"] --> SND
    LS --> ADA
    BN["bin/bench --strategies (new)"] --> LS
    BN -- "CSV" --> BS["bench_strategy.csv (new)<br/>adaptive_trace.csv (new)"]
```

ASCII fallback:

```
            +------------------------- UDP 9002 ACK/NAK + window stats (new) ---------------+
            v                                                                               |
 bin/vehicle --UDP 9000 (header v2)--> bin/channel --UDP 9001--> bin/tollgate               |
  sender.c   stop-and-wait (new)        noise + sidecar           receiver.c (new): decode  |
  adapt.c    ladder controller (new)    schedule.c (new):         never sees the sidecar    |
                                         toll -> hospital -> toll window.c (new): last W ---+
                                                                  --jsonl FILE (new)
                                                                        |
                                              tools/dashboard.py (new) <+  http://127.0.0.1:8050

 bin/bench --strategies (new) --> linksim.c (new, same sender + adapt + receiver, simulated clock)
                              --> results/bench_strategy.csv, adaptive_trace.csv (new)
 feedback.c (new): 22-byte ACK/NAK format
```

### 3. What changed

| component | change | files / modules | commit |
|---|---|---|---|
| tollgate receive path | decode moved into a module with no parameter for the truth; `rx_classify` is the only sidecar reader | `src/receiver.c`, `include/receiver.h`, `tests/test_receiver.c` | `04a7423` |
| feedback channel | ACK/NAK on 9002 (22 bytes), `--fb-drop` loss injection | `src/feedback.c` | `5c3040a` |
| strategies | `--strategy fec/arq/harq/adaptive`; one stop-and-wait state machine for vehicle and simulator | `src/sender.c`, `src/apps/vehicle.c`, `tollgate.c` | `5c3040a` |
| wire format | header v2 (+`attempt`, +`strategy`) | `src/packet.c`, `include/packet.h` | `5c3040a` |
| simulator | in-process link with simulated RTT, airtime at 160 kbit/s | `src/linksim.c` | `5c3040a` |
| controller (first version) | ladder + sliding window, evaluated on **every** feedback, up threshold 0.15 (committed inside the ARQ commit) | `src/adapt.c`, `src/window.c` | `5c3040a` |
| controller (redesign) | one decision per W=32 fresh frames, up 0.20, probe back-off | `src/adapt.c`, `include/adapt.h`, `tests/test_adapt.c`, `tests/test_linksim.c` | `04e8807` |
| channel | `--schedule toll:N,hospital:M,...` (counts transmissions) | `src/schedule.c`, `src/apps/channel.c` | `04e8807` |
| observability | tollgate `--jsonl`, Python stdlib dashboard, `make dashboard` | `tools/dashboard.py`, `tools/dashboard.sh`, `src/apps/tollgate.c` | `abcfd0b` |
| docs | PROGRESS, README results, PLAN checkboxes, test count fix | `STATUS.md`, `README.md`, `ROADMAP.md`, `tests/test_cli.c` | `fad9808`, `fc5dc28` |

### 4. Why

| change | problem it solved / question it answered | evidence |
|---|---|---|
| `src/receiver.c` | "could the decoder cheat with the sidecar?" (decision #6) | `tests/test_receiver.c`: 2100 corrupted frames, all 7 codes, sidecar present / zeroed / garbage / stripped -> bit-identical output (DESIGN_DECISIONS.md "Phase 3") |
| ARQ baseline | the "retransmission" strawman the project argues against | schedule channel, seed 42: ARQ 0.8194 success, 1.7828 retx/frame, p99 94.009 ms (`bench_strategy.csv`) |
| Hybrid ARQ (type I, #17) | FEC alone leaves failed frames lost | best plain FEC (RS t=4) 0.9136 vs HARQ RS t=4 1.0000 on the schedule channel (`bench_strategy.csv`) |
| vehicle decides, tollgate reports (#13) | the sender owns the code; the receiver stays stateless | DESIGN_DECISIONS.md "Week 2 - task 3" |
| step down only on a binomial prediction (#14) | corrected-bit counts are not comparable across codes | DESIGN_DECISIONS.md "Week 2 - task 3"; test `predicted_failure_matches_binomial_maths` |
| one decision per window (#15) | first controller flapped | 14 level changes in 3000 steady toll frames -> 0 (DESIGN_DECISIONS.md); test `adaptive_does_not_oscillate_on_a_steady_channel` |
| `--schedule` | a controller can only be shown on a channel that changes | adaptive 0.9994 success, 8 level changes, p99 33.211 ms (`bench_strategy.csv`, schedule) |
| shared `sender.c` + `linksim.c` | benchmark must measure the same protocol as the UDP demo | DESIGN_DECISIONS.md "Week 2 - task 2" |
| dashboard, stdlib only (#19) | nothing to install on the presentation laptop | DESIGN_DECISIONS.md "Week 2 - task 4" |

### 5. Wire format / interface changes

| item | v0.1 | v0.2 |
|---|---|---|
| DATA/END header | 14 bytes, version 1 | **16 bytes, version 2**: + `attempt` (offset 14), + `strategy` (offset 15) |
| feedback packet | none | **new**, 22 bytes: magic "FB", ver, type ACK/NAK, seq, attempt, level, win_n, win_fail, win_corrected, win_bits |
| port 9002 | reserved | **used**: tollgate -> vehicle, never corrupted, can be lost (`--fb-drop`) |
| new CLI flags | - | `--strategy --rtt-ms --timeout-ms --max-retries --fb-drop --window --up-threshold --down-threshold --start-level --schedule --strategies --trace --jsonl` |
| new make targets | - | `demo-arq demo-harq demo-adaptive dashboard` |
| new bench outputs | - | `results/bench_strategy.csv`, `results/adaptive_trace.csv`; PNGs 10 -> 12 (`strategy_comparison.png`, `adaptive_level.png`) |

### 6. Test and verification

| test | what it checks | result |
|---|---|---|
| 20 unit-test binaries, 123 test cases (`tests/test_*.c` at `fc5dc28`) | new: `test_receiver`, `test_feedback`, `test_adapt`, `test_linksim`, `test_schedule` | pass in the original session (STATUS.md at `fc5dc28`) |
| `tests/test_integration.sh` | + ARQ, HARQ with 30% injected feedback loss, adaptive on toll->hospital->toll, JSON-lines log + dashboard HTTP server | pass in the original session |
| `tests/test_dashboard.py` | dashboard state parsing | **referenced by the Makefile but not in git** (see failure 3) |
| fresh checkout of `fc5dc28`: `make test` | **(re-run for this document)** | **fails**: exit 2, `python3: can't open file '.../tests/test_dashboard.py'` (all C tests and the integration script passed before that line) |

### 7. What failed / what we got wrong

1. **The first adaptive controller flapped.** Expected: hysteresis (up 0.15 / down 0.05) keeps
   the level stable on a steady channel. Actual: it went L1 <-> L2 **14 times in 3000 steady toll
   frames** (DESIGN_DECISIONS.md "Week 2 - task 3"). Root cause: the rule was evaluated on every feedback
   over a *sliding* window (`5c3040a`, `include/adapt.h`: "evaluated on every feedback"), so one
   unlucky stretch gave the up-rule ~W correlated chances to fire. Fixed in `04e8807`: one decision
   per W fresh frames on disjoint windows, up threshold 0.20, probe back-off; 0 changes, guarded by
   `adaptive_does_not_oscillate_on_a_steady_channel` (`tests/test_linksim.c`).
2. **"RS + interleaving" at the top of the ladder made RS worse.** Expected (task spec): the
   interleaver strengthens the top rung. Actual (DESIGN_DECISIONS.md, hospital, 5000 frames): RS t=4
   **83.4%** alone, **77.4 / 69.5 / 59.8%** with bit interleaving d = 2 / 4 / 8.
   **(re-run for this document**, UDP pipeline, hospital, seed 42, 5000 frames: 84.1 / 78.2 / 70.4 /
   60.6%; RS t=8 alone 95.8%.) Root cause: a bit interleaver spreads one burst over *more* bytes,
   the opposite of what a byte-symbol code wants. Handled: ladder top = RS t=8 without interleaving
   (decision #12); RS t=8 reaches 95.3% hospital / 99.96% toll (DESIGN_DECISIONS.md).
3. **`make test` failed on a fresh clone.** `abcfd0b` added `tests/test_dashboard.py` to the
   Makefile, but `.gitignore` line `tests/test_*` (exceptions only for `.c` and `.sh`) hid the file,
   so it was never committed. The original session passed because the file existed locally.
   Confirmed for this document (exit 2 at `fc5dc28`). Fixed in v0.3 by `892bd77` (file added,
   `!tests/test_*.py` exception).
4. **Wrong test count in STATUS.md.** `fad9808` said "21 unit-test binaries"; there were 20.
   Fixed in `fc5dc28`.
5. **The `v0.2-week2` tag push was reported as done, then as impossible.** `fc5dc28` STATUS.md:
   "tagged `v0.2-week2` ... All pushes succeeded". `c4d7bf4` corrected it: "exists only locally:
   the session proxy refused tag pushes with HTTP 403". Today `git ls-remote --tags origin` shows
   `v0.2-week2` -> `fc5dc28` on GitHub, so the tag was pushed at a later, unrecorded point and
   STATUS.md is now outdated (see D. Doc issues).
6. **HARQ does not rescue weak codes on bursts.** Hospital only, seed 42 (`bench_strategy.csv`):
   HARQ + Hamming(7,4) 0.9200 success with 1.2528 retx/frame and p99 97.011 ms, HARQ + BCH(31,16)
   0.9622 / 0.9646 / 97.778 ms, while HARQ + RS t=4 reaches 1.0000 / 0.2034 / 53.609 ms. Root cause:
   type I HARQ repeats the same weak code; each copy fails for the same reason (bursts). Reported;
   it is why the ladder ends in RS.

### 8. Decisions still open / limitations

- HARQ is type I, no soft combining (#17); no incremental redundancy.
- RTT 20 ms, timeout 100 ms and 160 kbit/s are assumptions (#18); RTT sensitivity came only in v0.3.
- BER^ ignores failed frames, so it under-estimates on bursty channels (STATUS.md "Known issues").
- Adaptive flags (`--window`, thresholds) must be given to vehicle **and** tollgate.
- The tollgate's duplicate filter keeps `seq mod 65536`.
- The comparison baselines did not yet include "always use the strongest code" with HARQ.

---

## v0.3 - Final polish + overnight A: fair baselines, statistics, failure injection

### 1. Summary

No new process; the architecture was *hardened and measured*. A goodput metric and the
"always strongest code" baselines (HARQ + RS t=4 / t=8) showed that the adaptive controller's main
claim did not hold. The channel gained header-corruption injection, the build gained ASan/UBSan and
a fuzz test, the benchmark gained 20-seed statistics with confidence intervals, and the controller
gained a second, pre-registered ladder (`--ladder rs`). Git range `a6c204e`..`754e3e9`
(polish `a6c204e`, `e00eef7`, `c4d7bf4`; overnight A `892bd77`, `550ca0d`, `dd89f72`, `af91465`,
`955da8c`, `ac2adbb`, `b409980`, `bde2409`; gate A `754e3e9`).

### 2. Architecture diagram

```mermaid
flowchart LR
    V["bin/vehicle<br/>sender + adapt<br/>--ladder mixed or rs (new)"] -- "UDP 9000" --> C["bin/channel<br/>noise + sidecar<br/>--corrupt-header P (new)"]
    C -- "UDP 9001" --> T["bin/tollgate<br/>receiver: validate before seq bookkeeping (changed)"]
    T -. "UDP 9002 ACK/NAK + window stats" .-> V
    I["tests/inject.c (new)<br/>malformed packets"] -- "bad datagrams" --> T
    I -- "bad datagrams" --> C
    B["bin/bench --strategies --seeds N (new)"] --> ST["stats: mean + 95% CI, Student t (new)"]
    B --> L["linksim"]
    B -- "CSV" --> R["bench_strategy_seeds*.csv (new)<br/>bench_ladder_rs_seeds*.csv (new)<br/>bench_rtt_5/20/50.csv (new)"]
    H["tools/header_experiment.sh (new)"] --> HC["header_corruption.csv (new)"]
    S["make sanitize (new)<br/>ASan + UBSan, all tests"] -.-> B
    F["tests/test_fuzz.c (new)<br/>450k parser inputs"] -.-> T
```

ASCII fallback:

```
 bin/vehicle --UDP 9000--> bin/channel ----------------UDP 9001--> bin/tollgate
  sender + adapt            noise + sidecar                         receiver: validate packet
  --ladder mixed|rs (new)   --corrupt-header P (new):               BEFORE seq bookkeeping (changed)
                            1-4 header bit flips, END exempt        empty datagram = malformed (changed)
      ^                                                                  |
      +-------------------- UDP 9002 ACK/NAK + window stats -------------+

 tests/inject.c (new) ----- malformed / truncated / duplicate datagrams --> channel, tollgate
 tests/test_fuzz.c (new) -- 200k packet_parse + 200k feedback_parse + 50k --schedule inputs
 make sanitize (new) ------ every unit + integration test under ASan + UBSan (build/asan)
 bin/bench --strategies --seeds 20 (new) --> stats.c (new, Student t) --> *_seeds*.csv
 bin/bench --rtt-ms 5/20/50 ----------------------------------------------> bench_rtt_*.csv
 tools/header_experiment.sh (new) ----------------------------------------> header_corruption.csv
```

### 3. What changed

| component | change | files / modules | commit |
|---|---|---|---|
| docs | DEMO.md becomes the single demo guide | `docs/DEMO.md`, `README.md` | `a6c204e` |
| evaluation | goodput metric; baselines HARQ + RS t=4 / t=8 and FEC RS t=8 | `src/linksim.c`, `tools/bench.c`, `tools/plot.py` | `e00eef7` |
| evaluation | RTT sensitivity 5 / 20 / 50 ms | `Makefile`, `tools/plot.py` | `c4d7bf4` |
| tests | restore hidden `tests/test_dashboard.py`, `.gitignore` exception | `.gitignore`, `tests/test_dashboard.py` | `892bd77` |
| channel | `--corrupt-header P` (separate RNG stream) | `src/packet.c`, `src/apps/channel.c` | `550ca0d` |
| tollgate / net | empty datagram, seq stealing and `SO_REUSEADDR` bugs fixed | `src/netutil.c`, `src/apps/tollgate.c` | `550ca0d` |
| tests | failure injection | `tests/inject.c`, `tests/test_packet.c`, `tests/test_failure.sh`, `tools/header_experiment.sh` | `550ca0d` |
| build | `make sanitize` (ASan + UBSan), seeded fuzz test | `Makefile`, `tests/test_fuzz.c` | `dd89f72` |
| RS decoder | output the received bytes on failure (was uninitialized) | `src/rs.c`, `tests/test_rs.c` | `dd89f72` |
| statistics | `--seeds N`, mean +- 95% CI (Student t), paired differences | `src/stats.c`, `tests/test_stats.c`, `tools/bench.c` | `af91465` |
| controller | pre-registration, then `--ladder {mixed,rs}`, then the result | `DESIGN_DECISIONS.md`; `src/adapt.c`; `tools/bench.c` | `955da8c`, `ac2adbb`, `b409980` |
| docs | PROGRESS, gate A number corrections | `STATUS.md`, `DESIGN_DECISIONS.md` | `bde2409`, `754e3e9` |

### 4. Why

| change | problem it solved / question it answered | evidence |
|---|---|---|
| goodput + HARQ RS t=4/t=8 baselines | "is adaptive better than always using the strongest code?" was never measured | single seed: adaptive 0.505, HARQ RS t=8 0.490, HARQ RS t=4 0.612 (DESIGN_DECISIONS.md "Final polish"; `bench_strategy.csv` regenerated: 0.5047 / 0.4903 / 0.6121) |
| RTT sensitivity | RTT 20 ms is an assumption (#18) | p99 ARQ 26.509 -> 94.009 -> 229.009 ms, adaptive 10.712 -> 33.212 -> 78.212 ms; rankings unchanged (REPORT 7.3, `bench_rtt_*.csv`) |
| failure injection | the header is never corrupted (#1) - what if it is? | FEC, P=0.01: 23 of 24 corrupted headers cost a frame; HARQ, P=0.2: tollgate claims 2003 CORRECT of 2000 sent (DESIGN_DECISIONS.md #1) |
| sanitizers + fuzzing | C code with hand-made parsers and decoders | 1 real bug (RS), 0 parser bugs in 450k inputs (DESIGN_DECISIONS.md "task 2") |
| 20 seeds + paired CIs (#22) | single-seed differences of a few frames are noise | HARQ RS t=4 p99 is 32.4 ms in 11 seeds and >= 40 ms (53.6 ms) in 9 (`bench_strategy_seeds_raw.csv`, counted for this document) |
| all-RS ladder (#23) | was the *ladder*, not the controller, the reason adaptive lost? | goodput 0.584 +- 0.004 vs 0.504 mixed, paired +0.0799 +- 0.0050 (`bench_ladder_rs_seeds*.csv`) |

### 5. Wire format / interface changes

| item | v0.2 | v0.3 |
|---|---|---|
| packet / feedback format | header v2, feedback 22 bytes | unchanged |
| receive validation | seq bookkeeping before validation; 0-byte datagram = idle timeout | **validate first**; 0-byte datagram = malformed (`NET_EMPTY`) |
| UDP bind | `SO_REUSEADDR` | removed: second instance exits with `Address already in use` (exit 1) |
| new CLI flags | - | `--corrupt-header P` (channel), `--seeds N` (bench), `--ladder {mixed,rs}` (vehicle/tollgate/bench) |
| new make targets | - | `sanitize`; `make bench` now also writes RTT, 20-seed and all-RS CSVs |
| new scripts | - | `tools/header_experiment.sh`, `tests/test_failure.sh` |
| plots | 12 PNGs | 16 PNGs (+ `goodput_vs_success`, `rtt_sensitivity`, `goodput_vs_success_ci`, `ladder_rs_experiment`) |

### 6. Test and verification

| test | what it checks | result |
|---|---|---|
| 23 unit-test binaries, 143 test cases (`tests/test_*.c` at `754e3e9`) | new: `test_packet` (malformed / truncated / header corruption), `test_fuzz` (450k inputs), `test_stats` (CI maths) | pass, gcc 13 and clang (STATUS.md) |
| `tests/test_integration.sh` | + adaptive with `--ladder rs`, `--seeds` smoke test | pass (STATUS.md) |
| `tests/test_failure.sh` (new, ports 19200-19202) | garbage, empty, truncated, duplicate, lost END, port in use, L=4096 bursts, 100% feedback loss | pass (STATUS.md) |
| `tests/test_dashboard.py` | now in git | pass (STATUS.md) |
| `make sanitize` (gcc) | all of the above under ASan + UBSan | no findings after the RS fix (STATUS.md) |
| `make sanitize CC=clang` | same with clang | **not run**: clang ASan runtime missing in the container (DESIGN_DECISIONS.md) |

### 7. What failed / what we got wrong

1. **The adaptive controller's main claim did not hold.** Expected: "adaptive reaches the
   strongest code's reliability at clearly lower overhead". Actual (schedule channel, seed 42,
   `e00eef7`): adaptive 99.94% / goodput 0.505, HARQ + RS t=8 100% / 0.490 (only ~3% more
   overhead), HARQ + RS t=4 100% / **0.612**. Root cause: the ladder is ordered by burst
   robustness, not by code rate; its lower rungs (Hamming 0.571, BCH 0.449 / 0.516) cost *more*
   parity than RS t=4 (0.667), so stepping down saves nothing. Handled: reported honestly in
   README, DESIGN_DECISIONS.md and STATUS.md; an all-RS ladder was tested later (item 7).
2. **The single-seed conclusion "RS t=4 beats adaptive on every metric" was wrong.** Corrected by
   the 20-seed run (`af91465`): HARQ + RS t=4 wins goodput (0.613 vs 0.504, paired -0.1083 +- 0.0030)
   and success (99.998% vs 99.95%), but adaptive has fewer retransmissions (0.069 vs 0.088 per
   frame) and a lower p99 (33.2 ms vs 42 +- 5 ms). Root cause: seed 42 was lucky for RS t=4
   (p99 32.4 ms; 9 of 20 seeds give 53.6 ms), and retransmissions were already lower for adaptive
   in the single-seed run (0.076 vs 0.089). Handled: README table switched to 20-seed means.
3. **Failure injection found 3 real bugs** (`550ca0d`, DESIGN_DECISIONS.md "Overnight A - task 1"):
   (a) a 0-byte datagram was read as "idle timeout" and **stopped** the tollgate/channel;
   (b) an invalid packet **stole the seq** of a later valid one, which was then dropped as a
   duplicate and counted as DETECTED_FAIL; (c) `SO_REUSEADDR` let a **second process bind the same
   UDP port** and silently share packets. All fixed and tested. A fourth bug was in the test
   script itself: `check "x" [ a ] && [ b ]` only checked `a`; fixed with `eval`.
4. **Sanitizers found 1 real bug** (`dd89f72`): `rs_decode` returned -1 without writing `data`, so
   failed RS frames logged uninitialized bytes as `tag_rx` / `crc_ok` (classification unaffected).
   Found because `tests/test_receiver.c` (decode twice, compare) failed only in the ASan build.
   Fixed; test `failure_outputs_received_data_bytes`.
5. **Header corruption causes misrouting the tollgate cannot see** (open). With HARQ at P = 0.2 the
   tollgate reports **2003 CORRECT of 2000 frames sent**; with plain FEC at P = 0.05, 96 of 100
   corrupted headers cost a frame (4.8% of all frames). Root cause: no header checksum; a flipped
   seq bit files a frame under another seq. **Left open**: the fix (header CRC-16) changes the
   wire format.
6. **Two DESIGN_DECISIONS.md numbers were wrong** and were corrected by gate A (`754e3e9`): HARQ frames
   "lost or misrouted" at P = 0.01 / 0.05 were written as 0.15% / 0.6% but are **0.1% / 0.5%**
   (the old numbers included the "failed" column); the p99 difference vs HARQ RS t=8 was written as
   "+0.002 ms" and became "+0.001-0.002 ms (real decode-time jitter, differs per run)".
   No README number was changed by gate A.
7. **The all-RS ladder held its pre-registered hypothesis but still lost to a fixed code.**
   Criterion met (goodput 0.584 +- 0.004 vs 0.504 mixed and 0.491 RS t=8; success 99.999%), but
   vs fixed HARQ + RS t=4 the paired goodput difference is **-0.0284 +- 0.0038** on the schedule
   channel and it only ties on toll alone (-0.0007 +- 0.0009). Reported as a new experiment;
   default stays `--ladder mixed`.
8. **The mixed adaptive ladder loses to "always RS t=8" on each single profile** (found in the
   CSVs while writing this document, not stated in the other docs). Paired, 20 seeds
   (`bench_strategy_seeds_diff.csv`): hospital only, goodput **-0.0033 +- 0.0007**; toll only, p99
   **+21.2 ms**. Its +0.0133 goodput advantage exists only on the switching schedule channel.
9. **Dashboard labels are wrong with `--ladder rs`** (level numbers right, axis names from the
   mixed ladder). Left open (DESIGN_DECISIONS.md "task 4").

### 8. Decisions still open / limitations

- Header protection (CRC-16, new header version) - open problem in STATUS.md.
- Default ladder for the demo: `mixed` (all docs use it) vs `rs` - team decision listed in STATUS.md.
- p99 of HARQ codes is bimodal across seeds, so its t-interval is approximate (#22 caveat).
- `make sanitize CC=clang` not run; macOS / WSL not run in this version.
- No controller can choose "fixed RS t=4" when that is best (REPORT section 10, future work).

---

## v0.4 - Overnight B/C and docs: CI, report, LDPC, demo fixes

### 1. Summary

The runtime architecture stayed the same; around it the project gained continuous integration
(ubuntu + macOS + sanitizers on GitHub Actions), a sourced report (`docs/REPORT.md`) with a
number-checking gate, and an optional eighth code, LDPC(256,128) with a Gallager bit-flipping
decoder, benchmarked in a separate sweep so no earlier result changed. A real WSL run of the demo
guide then fixed the demo targets and timings. Git range `a5bc434`..`53c6109` (overnight B `a5bc434`,
`5b4c301`, `ecd770f`; gate B `9933409`; overnight C `439c5bc`, `e10f13f`, `539acca`; docs-fix
`de1059b`, `b9f8436`; docs-timings `53c6109`).

### 2. Architecture diagram

```mermaid
flowchart LR
    V["bin/vehicle<br/>--code ldpc (new)"] -- "UDP 9000" --> C["bin/channel"]
    C -- "UDP 9001" --> T["bin/tollgate"]
    T -. "UDP 9002 ACK/NAK + window stats" .-> V
    subgraph LIB["build/libfeclink.a"]
        FEC["fec registry: 8 codes"]
        LD["ldpc: PEG H, Gaussian-elimination encoder,<br/>bit flipping, max 50 iterations (new)"]
        FEC --> LD
    end
    V --> FEC
    T --> FEC
    B["bin/bench --compare-codes (new)"] --> FEC
    B -- "CSV" --> R["bench_ldpc.csv (new)<br/>bench_ldpc_hospital.csv (new)"]
    DS["tools/demo.sh<br/>SHOWBITS switch (new)"] --> V
    GH["GitHub Actions CI (new)<br/>ubuntu + macOS make test, ubuntu make sanitize"] -.-> B
    REP["docs/REPORT.md (new)"] -.-> R
```

ASCII fallback:

```
 bin/vehicle --UDP 9000--> bin/channel --UDP 9001--> bin/tollgate
  --code ldpc (new)                                    decodes ldpc too (code id 7)
      ^                                                       |
      +------------- UDP 9002 ACK/NAK + window stats ---------+

 build/libfeclink.a: fec registry, 8 codes = none hamming74 hamming1511 secded84 bch157 bch3116 rs
                     + ldpc.c (new): (3,6)-regular LDPC(256,128), PEG-style seeded H,
                       Gaussian-elimination encoder, Gallager bit flipping (max 50 iterations)
 bin/bench --compare-codes bch157,bch3116,rs,rs_t8,ldpc (new) --> bench_ldpc.csv, bench_ldpc_hospital.csv
 tools/demo.sh: SHOWBITS env (new); make demo-harq now on the hospital profile (changed)
 .github/workflows/ci.yml (new): ubuntu-latest + macos-latest make test, ubuntu make sanitize
 docs/REPORT.md (new) <-- numbers checked against results/*.csv by gate B
```

The ladder, the controller and every strategy are unchanged: LDPC is not on any ladder.

### 3. What changed

| component | change | files / modules | commit |
|---|---|---|---|
| CI | GitHub Actions: build + `make test` on ubuntu-latest and macos-latest, `make sanitize` on ubuntu | `.github/workflows/ci.yml` | `a5bc434` |
| docs | report draft with numbers from the CSVs; first CI run recorded | `docs/REPORT.md`, `STATUS.md`, `ROADMAP.md` | `5b4c301`, `ecd770f` |
| docs | gate B: REPORT.md numbers checked; "tie within the noise" replaced by a paired difference | `docs/REPORT.md`, `STATUS.md` | `9933409` |
| codes | LDPC(256,128) + bit-flipping decoder, tests first | `src/ldpc.c`, `include/ldpc.h`, `tests/test_ldpc.c` | `439c5bc` |
| codes / bench | `--code ldpc` in the FEC registry; `bench --compare-codes`; LDPC plots | `src/fec.c`, `include/fec.h`, `tools/bench.c`, `tools/plot.py`, `Makefile` | `e10f13f` |
| docs | honest LDPC result in all docs | `DESIGN_DECISIONS.md`, `STATUS.md`, `ROADMAP.md`, `README.md`, `docs/REPORT.md` | `539acca` |
| demo | `SHOWBITS` switch; `demo-harq` uses the hospital profile; integration check | `Makefile`, `tools/demo.sh`, `tests/test_integration.sh` | `de1059b` |
| docs | DEMO.md after a real WSL run (setup, test output, talking script, bench/plots, dashboard) | `docs/DEMO.md`, `tools/plot.py`, `STATUS.md` | `b9f8436` |
| docs | DEMO.md timings from the measured WSL run | `docs/DEMO.md` | `53c6109` |

### 4. Why

| change | problem it solved / question it answered | evidence |
|---|---|---|
| CI on ubuntu + macOS | nobody had run the code on macOS; the fixed interface requires it | first run 38829733020 on `a5bc434`: 3 jobs green (STATUS.md); all 15 CI runs up to `53c6109` concluded "success" (GitHub Actions API, checked for this document) |
| REPORT.md + gate B | the teacher needs one sourced report | gate B: "no mismatches; all 8 figure paths exist" (STATUS.md "Gate B") |
| LDPC (optional stretch) | "would a modern code beat RS on our channels?" | hospital GE: LDPC 0.7902 vs RS t=8 0.9532 (`bench_ldpc_hospital.csv`) |
| separate `--compare-codes` sweep (#26) | existing results must not change | `bench.csv` / `bench_hospital.csv` identical to main's except timing (DESIGN_DECISIONS.md "Overnight C") |
| RS t=8 in the LDPC comparison | RS t=4 (rate 0.667) is not a same-rate rival for LDPC (0.500) | DESIGN_DECISIONS.md "Overnight C" |
| `demo-harq` on hospital | on the toll profile the 20-frame demo showed no retransmission at all | **(re-run for this document)** toll, seed 42, 20 frames: 0 retransmissions; hospital: 42 retransmissions, 4 frames given up |
| timings in DEMO.md | presenters need realistic durations | `53c6109`: demo-adaptive ~15 -> ~14 s, dashboard ~36 -> ~33 s, `make bench` ~2 -> ~1 min |

### 5. Wire format / interface changes

| item | v0.3 | v0.4 |
|---|---|---|
| code ids | 0..6 | + **7 = `ldpc`** (LDPC(256,128), 256 coded bits per frame); header format unchanged (still v2) |
| `--code` values | 7 | 8: `... rs ldpc` |
| new CLI flags | - | `--compare-codes <list>` (bench) |
| make | - | `SHOWBITS` variable (default 1 for `demo`/`demo-hospital`, 0 for `demo-arq`/`demo-harq`); `demo-harq` now `--profile hospital`; no new target |
| bench outputs | - | `results/bench_ldpc.csv`, `results/bench_ldpc_hospital.csv`; PNGs 16 -> 26 |
| CI | none | `.github/workflows/ci.yml` |

### 6. Test and verification

| test | what it checks | result |
|---|---|---|
| 24 unit-test binaries, 158 test cases (`tests/test_*.c` at `53c6109`) | new: `test_ldpc` (zero syndrome, all 1- and 2-bit errors corrected exhaustively, success only for codewords) | pass |
| `tests/test_integration.sh`, `tests/test_failure.sh`, `tests/test_dashboard.py` | + `--code ldpc` run, `SHOWBITS` check | pass |
| `make test` on `main` / this branch | **(re-run for this document)** | see section E |
| GitHub Actions CI | ubuntu + macOS `make test`, ubuntu `make sanitize` | 15 of 15 runs "success", latest run 38840463154 on `53c6109` (`main`) |
| macOS / WSL by hand | demo guide on a real laptop | WSL run reported in STATUS.md / `53c6109`; macOS only through CI |

### 7. What failed / what we got wrong

1. **LDPC did not beat Reed-Solomon on bursts, and was the slowest decoder.** Expected: a
   modern code at rate 1/2 is at least as good as RS t=8. Actual (seed 42, 5000 frames/point):
   best on random errors (BSC p=0.02: **0.9874** vs BCH(31,16) 0.9750, RS t=8 0.9608), but
   hospital GE **0.7902** vs RS t=4 0.8336 and RS t=8 0.9532, burst L=16 0.9690 and L=24 **0.6916**
   vs 1.0000 for both RS codes (`bench_ldpc*.csv`). Mean decode time 10.5 us vs RS t=4 3.3, RS t=8
   5.6, BCH(31,16) 6.9 us (STATUS.md; **re-run for this document**: 9.6 / 3.1 / 5.5 / 7.3 us -
   timing varies, ranking identical). Root cause: hard-decision bit flipping with a short random
   code; a frame it cannot fix runs all 50 iterations (up to ~50 us). Handled: reported, LDPC kept
   off the ladder; soft decoding left as future work.
2. **The first LDPC comparison used an unfair baseline.** RS t=8 (same rate 0.500) was added
   *after* the first LDPC run, because RS t=4 has rate 0.667 (DESIGN_DECISIONS.md "Overnight C"). Only the
   baseline set changed, not the LDPC code; stated openly in DESIGN_DECISIONS.md.
3. **The live ARQ/HARQ demos did not match the benchmark numbers in the talking script.**
   Expected: `make demo-arq` / `make demo-harq` illustrate the 5000-frame numbers. Actual
   **(re-run for this document**, old `demo-harq` command = toll, Hamming(7,4), seed 42, 20 frames):
   **0 retransmissions**, while `bench_strategy.csv` gives 0.0742 retx/frame for the same strategy
   on toll. On the hospital profile the 20-frame demo gives 2.10 retx/frame (42 in 20 frames) vs
   1.2528 in the benchmark. Root cause: 20 frames are far too few for a rate. Handled (`de1059b`,
   `b9f8436`): `demo-harq` moved to the hospital profile so retransmissions are visible, and the
   script row now shows `results/strategy_comparison.png` with the note "quote the benchmark".
4. **DEMO.md timings were guesses until a real run.** `53c6109` changed demo-adaptive ~15 -> ~14 s,
   dashboard ~36 -> ~33 s and `make bench` ~2 min -> ~1 min after a WSL run. `make bench` took
   **108 s** in this container (re-run for this document), so "~1 min" is host-dependent, and
   STATUS.md still says ~1:50 (see D).
5. **The LDPC construction could not reach girth 8.** The greedy PEG avoids 4-cycles but the
   default code has **girth 6** (DESIGN_DECISIONS.md "Overnight C"); every 1- and 2-bit error is still
   corrected. Left as is.

### 8. Decisions still open / limitations

- Header CRC-16 (misrouting, v0.3 item 5) still open.
- LDPC: no soft-decision (sum-product) decoder, no longer code; single seed for the LDPC sweep.
- `docs/REPORT.md` "needs a human proofread before submission" (STATUS.md).
- Run D (local tag, `make test` on the team's own macOS/WSL machines, rehearsal) is the listed next step.
- Software simulation only: no hardware, no over-the-air channel; RTT, bit rate and noise are assumptions.

---

## A. Architecture evolution at a glance

Counts computed with `git ls-tree` / `git show` at the last commit of each version
(`aa2aaac`, `fc5dc28`, `754e3e9`, `53c6109`). "Test cases" = `RUN(...)` lines in `tests/test_*.c`.

| | v0.1 (`aa2aaac`) | v0.2 (`fc5dc28`) | v0.3 (`754e3e9`) | v0.4 (`53c6109`) |
|---|---|---|---|---|
| processes | vehicle, channel, tollgate (+ in-process `bin/bench`) | same + `tools/dashboard.py` HTTP server | same | same |
| feedback channel | UDP 9002 reserved, unused | ACK/NAK + window stats, 22 bytes | same (+ loss / garbage tested) | same |
| packet header | v1, 14 bytes | v2, 16 bytes (+attempt, +strategy) | v2 (validation before seq bookkeeping) | v2 (+ code id 7) |
| codes (`--code`) | 7: none, hamming74, hamming1511, secded84, bch157, bch3116, rs | 7 | 7 | 8: + ldpc |
| strategies | FEC only | fec, arq, harq, adaptive | same + HARQ RS t=4 / t=8 baselines in the bench | same |
| controller | none | mixed ladder L0-L5, one decision per W=32 | + all-RS ladder (`--ladder rs`, 5 rungs) | unchanged |
| library modules (`src/*.c`) | 19 | 26 (+adapt, feedback, linksim, receiver, schedule, sender, window) | 27 (+stats) | 28 (+ldpc) |
| observability | console summary, coloured bit map, `--log`, `--summary-file` | + `--jsonl`, live dashboard | + `malformed=` / `invalid=` / `duplicates=` counters | + `SHOWBITS` switch |
| unit-test binaries / test cases | 15 / 91 | 20 / 123 | 23 / 143 | 24 / 158 |
| test scripts | `test_integration.sh` | same; `test_dashboard.py` referenced but missing | + `test_failure.sh`, `test_dashboard.py`, `make sanitize`, fuzz | same |
| CI | none | none | none | GitHub Actions: ubuntu + macOS `make test`, ubuntu `make sanitize` |
| `make bench` CSV files (incl. `_raw` / `_diff`) | 2 | 4 | 13 | 15 |
| tracked plots (`results/*.png`) | 10 | 12 | 16 | 26 |
| key limitation | bursts beyond a code's design lose frames; no retransmission | adaptive never compared with "always strongest code" | header unprotected (misrouting); adaptive below fixed HARQ + RS t=4 | LDPC loses on bursts; no hardware; header still unprotected |

## B. Results timeline

| version | headline result | number | source |
|---|---|---|---|
| v0.1 | best code, toll, random errors (BSC p=0.01) | BCH(31,16) 0.9986 (rate 0.516) | `bench.csv` |
| v0.1 | best codes, hospital GE p_gb=0.005 | BCH(31,16)+il8 0.8374 (rate 0.516), RS t=4 0.8336 (rate 0.667) | `bench_hospital.csv` |
| v0.1 | SILENT_WRONG | 0 of 1,395,000 frames per profile | `bench.csv`, `bench_hospital.csv` |
| v0.2 | strategies on the toll->hospital->toll schedule, seed 42 | adaptive 0.9994 success, 0.0764 retx/frame, p99 33.211 ms; ARQ 0.8194 / 1.7828 / 94.009 ms; HARQ BCH(31,16) 0.9940 / 0.1980 / 76.226 ms | `bench_strategy.csv` |
| v0.3 | goodput, seed 42 | adaptive 0.5047, HARQ RS t=8 0.4903, HARQ RS t=4 0.6121 | `bench_strategy.csv` |
| v0.3 | goodput, 20 seeds, mean +- 95% CI | adaptive 0.5043 +- 0.0025, HARQ RS t=8 0.4910 +- 0.0005, HARQ RS t=4 0.6126 +- 0.0012 | `bench_strategy_seeds.csv` |
| v0.3 | paired difference adaptive - HARQ RS t=4, 20 seeds | goodput -0.1083 +- 0.0030; retx -0.0194 +- 0.0025; p99 -8.74 +- 5.06 ms | `bench_strategy_seeds_diff.csv` |
| v0.3 | all-RS ladder, 20 seeds | goodput 0.5841 +- 0.0040, success 0.99999; paired vs HARQ RS t=4: -0.0284 +- 0.0038 | `bench_ladder_rs_seeds*.csv` |
| v0.3 | RTT sensitivity, p99 at 5 / 20 / 50 ms | ARQ 26.509 / 94.009 / 229.009 ms; adaptive 10.712 / 33.212 / 78.212 ms | `bench_rtt_*.csv` (as in REPORT 7.3; the regenerated CSV differs by 0.001 ms, timing noise) |
| v0.3 | header corruption, HARQ, P=0.2 | tollgate claims 2003 CORRECT of 2000 sent | `header_corruption.csv` (via DESIGN_DECISIONS.md #1; not regenerated for this document) |
| v0.4 | LDPC(256,128) vs RS t=8 (same rate) | BSC p=0.02: 0.9874 vs 0.9608; hospital GE: 0.7902 vs 0.9532; burst L=24: 0.6916 vs 1.0000 | `bench_ldpc.csv`, `bench_ldpc_hospital.csv` |

## C. Lessons learned

- **Evaluate a controller once per window of evidence, not on every sample.** A sliding window
  checked every frame gave correlated chances to fire and flapped 14 times (v0.2, failure 1).
- **Measure before adding a "standard" improvement.** The bit interleaver everyone expects to help
  cut RS t=4 from 83.4% to 59.8% (v0.2, failure 2).
- **Always include the trivial strong baseline.** "Always HARQ + RS t=4" beat the adaptive
  controller on goodput (0.613 vs 0.504); without that baseline the main claim would have stood
  (v0.3, failures 1 and 7).
- **One seed is an anecdote.** The single-seed "RS t=4 wins on every metric" flipped on
  retransmissions and p99 with 20 seeds; seed 42 was a lucky p99 (v0.3, failure 2).
- **Attack your own inputs.** Failure injection and sanitizers found 4 real bugs in code that had
  passed every functional test (v0.3, failures 3 and 4).
- **Test on a fresh clone, not the working copy.** A `.gitignore` pattern hid a test file, so
  `make test` failed for everyone but the author (v0.2, failure 3).
- **Small demos do not show rates.** 20 frames gave 0 retransmissions where the benchmark gives
  0.074/frame; quote the benchmark, show the demo (v0.4, failure 3).
- **Fair comparisons need equal rates.** LDPC first ran against RS t=4 (rate 0.667) before RS t=8
  (0.500) was added, and against the fair baseline it loses on bursts (v0.4, failures 1-2).

## D. Doc issues

Disagreements found while writing this document. None was fixed here (documentation-only task
limited to this file and two links); git and the CSVs are taken as correct.

| # | where | says | git / CSV / re-run says |
|---|---|---|---|
| 1 | `STATUS.md` line 4 | tag `v0.2-week2` "exists only locally" (HTTP 403) | `git ls-remote --tags origin`: `v0.2-week2` -> `fc5dc28` exists on GitHub |
| 2 | `STATUS.md` (CI section), `ROADMAP.md` (overnight B task 5) | later CI runs "still queued"; workflow "not yet run" | all 15 CI runs, up to `53c6109` on `main`, concluded "success" |
| 3 | `make bench` duration | DEMO.md "~1 min"; STATUS.md "~1:50"; REPORT.md "about 100 s" | 108 s in this container; host-dependent |
| 4 | `STATUS.md` docs-fix section | dashboard "120 ms/frame (~36 s + 3 s)" | `docs/DEMO.md` after `53c6109`: "~33 s of demo after a 3 s start" |
| 5 | `docs/DEMO.md` talking script rows 6 and 8 | single-seed numbers: HARQ RS "32 ms", adaptive "99.94%", goodput "0.505 against 0.490" | README uses the 20-seed means: p99 42 +- 5 ms for HARQ RS t=4 (32 ms was seed luck, 11 of 20 seeds), adaptive 99.95%, goodput 0.504 vs 0.491 |
| 6 | `README.md` intro "Codes:", `docs/DEMO.md` section 1 and flag table | code list without `ldpc` | `src/fec.c`, `bin/bench --help`, CONTRIBUTING.md list 8 codes incl. `ldpc` |
| 7 | `README.md` | "Decode latency is 2-8 microseconds per frame for every code" | REPORT.md 7.1: BCH(31,16) 9.135 us; regenerated `bench.csv`: up to 10.496 us (BCH(31,16)+il8); LDPC up to ~50 us |
| 8 | `docs/DEMO.md` section 2, `CONTRIBUTING.md` | port 9002 "reserved for week-2 feedback" | used since `5c3040a` (ACK/NAK + window stats) |
| 9 | `include/packet.h` comment | "version (1)" | `#define PACKET_VERSION 2` since `5c3040a` (code comment; not changed here) |
| 10 | `DESIGN_DECISIONS.md` decision #12 | RS t=4 hospital 83.4% -> 77.4 / 69.5 / 59.8% with d = 2 / 4 / 8 | no CSV holds RS + interleaver; UDP re-run (seed 42): 84.1 / 78.2 / 70.4 / 60.6% - same trend, not bit-identical |
| 11 | commit `5c3040a` message | "Stop-and-Wait ARQ and Hybrid ARQ ..." | the commit also added `src/adapt.c` and `src/window.c` (the first, flapping controller) |
| 12 | `STATUS.md` "Decisions to review" | decision 20 listed before 19 | ordering only |

## E. How to reproduce each version

```sh
git clone https://github.com/PavitranVC/Adaptive-FEC-Link.git && cd Adaptive-FEC-Link
git checkout <commit>      # detached HEAD
make && make test
make bench                 # optional: regenerates results/*.csv (git-ignored)
```

| version | commit | `make` + `make test` | verified for this document |
|---|---|---|---|
| v0.1 | `aa2aaac` | passes | **yes**: `make` exit 0, `make test` exit 0, "Integration test passed." (plot test skipped, no matplotlib) |
| v0.2 | `fc5dc28` (tag `v0.2-week2`) | **fails on a fresh clone**: `tests/test_dashboard.py` missing (v0.2 failure 3); all C tests and the integration script pass before that line | **yes**: exit 2, as described |
| v0.3 | `754e3e9` | expected to pass (STATUS.md: all green, gcc 13 + clang) | no |
| v0.4 | `53c6109` (`main`) | passes; also green in CI (ubuntu, macOS, sanitize) | **yes** (this branch = `53c6109` + docs): "All unit tests passed.", "Integration test passed.", "Failure-injection test passed.", "dashboard tests passed", exit 0 |
