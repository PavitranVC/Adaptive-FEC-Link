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
make            # builds bin/vehicle bin/channel bin/tollgate bin/bench
make test       # unit tests + integration test
make demo       # one-terminal live demo (toll plaza profile)
make demo-hospital
make compare    # hamming74 vs bch157, same seed, side-by-side summary
make bench      # results/bench.csv
pip install -r requirements.txt && make plots   # results/*.png
```

Three-terminal demo and all flags: see [`docs/DEMO.md`](docs/DEMO.md).

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

## Repository

See `CLAUDE.md` for the folder layout and coding rules, `PLAN.md` for the roadmap,
`PROGRESS.md` for the current state and `DECISIONS.md` for design decisions.
