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

## Repository

See `CLAUDE.md` for the folder layout and coding rules, `PLAN.md` for the roadmap,
`PROGRESS.md` for the current state and `DECISIONS.md` for design decisions.
