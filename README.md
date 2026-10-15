# Adaptive Multi-Error FEC Link Layer for High-Interference Environments

University Computer Networks project, extending **"Fast-Lane FEC for Automated Toll Booths"**.

A vehicle's 96-bit RFID tag ID travels to a roadside unit over a noisy wireless link. Instead of
asking for a retransmission (slow - the car is moving), the receiver **corrects errors in place**
with forward error correction (FEC). This project simulates that link in software and compares
several error-correcting codes under two interference profiles:

| Profile | Meaning (modelling assumption) |
|---------|--------------------------------|
| `toll` - TOLL_PLAZA (primary) | mostly random bit flips, occasional short bursts |
| `hospital` - HOSPITAL_IMAGING (secondary) | strong bursty interference near MRI/CT/electrosurgical equipment |

Codes: Hamming(7,4), Hamming(15,11), extended Hamming SECDED(8,4), BCH(15,7) t=2,
BCH(31,16) t=3 and (phase 6) shortened Reed-Solomon over GF(256) with a block interleaver.
Every code, the CRC-32, the RNG and the Galois-field arithmetic are implemented from scratch in C11.

## Quick start

```sh
make                 # build bin/vehicle bin/channel bin/tollgate bin/bench
make test            # unit tests + integration test
make demo            # one-terminal live demo (toll plaza profile)
make demo-adaptive   # adaptive FEC on a toll -> hospital -> toll channel
make dashboard       # live web dashboard at http://127.0.0.1:8050/
```

Setup per OS (WSL / Ubuntu / macOS), every demo, plots, troubleshooting and the presentation
script: **[`docs/DEMO.md`](docs/DEMO.md)**.

## Architecture

```
 bin/vehicle --UDP 9000--> bin/channel --UDP 9001--> bin/tollgate
  (encoder)               (noise injector,           (decoder, CRC check,
                           ground-truth logger)       classification, stats)
            <------------- UDP 9002 feedback (week 2: HARQ / adaptive) ----
```

Each received frame is classified as **CORRECT**, **DETECTED_FAIL** (CRC failed - the system knows
it must not bill this read) or **SILENT_WRONG** (CRC passed but the ID is wrong - the dangerous
case, measurable only because the channel logs ground truth).

## First results (`make bench`, 5000 frames per point, seed 42)

Frame success rate (CORRECT / frames). GE = Gilbert-Elliott with p_gb = 0.005.

| code | rate | toll, BSC p=0.01 | toll, GE | hospital, GE | burst L=16 |
|------|------|------|------|------|------|
| uncoded           | 1.00  | 0.270 | 0.333 | 0.395 | 0.000 |
| Hamming(7,4)      | 0.571 | 0.935 | 0.775 | 0.417 | 0.001 |
| Hamming(7,4) + interleaver d=8 | 0.571 | 0.936 | 0.939 | 0.611 | 0.096 |
| BCH(31,16)        | 0.516 | 0.999 | 0.960 | 0.494 | 0.013 |
| BCH(31,16) + interleaver d=8   | 0.516 | 0.998 | 0.998 | 0.837 | 1.000 |
| RS(24,16) t=4     | 0.667 | 0.963 | 0.976 | 0.834 | 1.000 |

* Toll plaza (random errors): BCH(31,16) is best; Hamming(7,4) is a cheap, fast fallback.
* Hospital (bursts): bit-level codes collapse; Reed-Solomon (byte symbols) or an interleaver
  restores them. RS does it at the highest code rate.
* SILENT_WRONG was **0 in every run** (155,000 frames per code and profile): CRC-32 catches every
  miscorrection, so FEC failures cost a re-read, never a wrong bill.
* Decode latency is 2-8 microseconds per frame for every code (`results/latency.png`).

Plots: `results/*.png` (`_hospital` suffix for the hospital profile).

## Week 2: retransmissions and adaptive FEC

`--strategy fec|arq|harq|adaptive` (default `fec` = everything above). ACK/NAK feedback flows
tollgate -> vehicle on UDP 9002. The adaptive controller climbs a code ladder (uncoded ->
Hamming(7,4) -> BCH(15,7) -> BCH(31,16) -> RS t=4 -> RS t=8) using the receiver's sliding-window
statistics, with hysteresis, and falls back to HARQ on CRC failure.

On a channel that switches toll -> hospital -> toll (`make bench`, 5000 frames per run, RTT 20 ms),
**mean of 20 seeds (42..61) +- 95% confidence interval** (Student t, 19 degrees of freedom;
`results/bench_strategy_seeds.csv`). Goodput = correctly delivered payload bits / all transmitted
bits (parity + retransmissions).

| strategy | correct | goodput | code rate | retx / frame | p99 latency |
|---|---|---|---|---|---|
| FEC RS t=8 (no retransmission) | 98.17 +- 0.10% | 0.491 +- 0.001 | 0.500 | 0 | 12 ms |
| Stop-and-Wait ARQ (uncoded) | 82.44 +- 0.28% | 0.296 +- 0.002 | 1.000 | 1.78 +- 0.01 | 94 ms |
| Hybrid ARQ, BCH(31,16) | 99.32 +- 0.06% | 0.428 +- 0.001 | 0.516 | 0.196 +- 0.002 | 76 ms |
| Hybrid ARQ, RS t=8 (strongest code) | 100.00% | 0.491 +- 0.001 | 0.500 | 0.018 +- 0.001 | 33 ms |
| **Hybrid ARQ, RS t=4** | **99.998 +- 0.003%** | **0.613 +- 0.001** | 0.667 | 0.088 +- 0.002 | **42 +- 5 ms** (32 or 54 ms, see below) |
| Adaptive ladder | 99.95 +- 0.01% | 0.504 +- 0.002 | 0.543 | 0.069 +- 0.002 | 33 ms |

Honest reading (multi-seed, paired differences on the same seeds): adaptive's goodput advantage
over always-RS t=8 is real but small: +0.013 +- 0.002 (+2.7%), at 0.05 percentage points lower
success. Fixed HARQ + RS t=4 has clearly the best goodput (+0.108 over adaptive) and the best
reliability, but it is **not** better on every metric (the single-seed run said so): adaptive needs
fewer retransmissions (0.069 vs 0.088 per frame) and has the lower p99 latency. RS t=4's p99 is
bimodal across seeds - 32.4 ms in 11 seeds, 53.6 ms in 9 - because about 1% of its frames need a
second retransmission; seed 42 happened to land on 32 ms. The ladder's "cheap" rungs (Hamming
0.571, BCH 0.449/0.516) have a *lower* code rate than RS t=4 (0.667), so stepping down in quiet
periods does not save bits. See `results/goodput_vs_success_ci.png` and `results/goodput_vs_success.png`.

![adaptive level](results/adaptive_level.png)

Demos: `make demo-arq`, `make demo-harq`, `make demo-adaptive`, `make dashboard` (live web page).

## Repository

See `CLAUDE.md` for the folder layout and coding rules, `PLAN.md` for the roadmap,
`PROGRESS.md` for the current state and `DECISIONS.md` for design decisions.
