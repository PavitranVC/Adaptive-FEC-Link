# Contributing

This file is the specification and the working agreement for the repository: what the system is,
which interfaces are fixed, how the code is laid out and the rules every change follows.
Current state and open problems: [`docs/STATUS.md`](docs/STATUS.md). Roadmap:
[`docs/ROADMAP.md`](docs/ROADMAP.md). Design decisions: [`docs/DESIGN_DECISIONS.md`](docs/DESIGN_DECISIONS.md).
Environment, test matrix, branch naming and release process: [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md).

**Before every commit: `make test` passes, `git status` shows no binaries, logs or CSVs staged.**

## Project specification

"Adaptive Multi-Error FEC Link Layer for High-Interference Environments" is a Computer Networks
project that extends the approved project "Fast-Lane FEC for Automated Toll Booths".
A vehicle's 96-bit RFID tag ID is sent to a roadside unit (tollgate) over a noisy link and is
corrected *in place* with forward error correction (FEC) instead of retransmission.
Software simulation only, no hardware.

Two noise profiles (parameters are **modelling assumptions**, see `include/profiles.h`):

* `TOLL_PLAZA` (primary): mostly random bit flips, short bursts.
* `HOSPITAL_IMAGING` (secondary): strong bursty interference near MRI/CT/electrosurgical equipment.

### Frame

```
 +----------------- header (NOT corrupted by channel) ----------------+---- coded payload ----+
 | magic | ver | type | code | rs_t | il | seq | nbits | attempt | strat |  FEC( ID96 || CRC32 ) |
 +--------------------------------------------------------------------+-----------------------+
```

* payload = 96-bit tag ID followed by CRC-32 (IEEE 802.3) over the 12 ID bytes = 128 bits,
  then encoded with the selected code (block code applied to consecutive k-bit chunks).
* The channel appends an *evaluation-only* ground-truth sidecar (clean codeword + flip count).
  The tollgate never uses it to decode, only to classify:
  * `CORRECT`       - CRC passes and decoded ID equals the true ID
  * `DETECTED_FAIL` - CRC fails (or the decoder flagged an uncorrectable block)
  * `SILENT_WRONG`  - CRC passes but the ID is wrong (only known thanks to ground truth)

### Processes (UDP on 127.0.0.1)

```
 vehicle --(9000)--> channel --(9001)--> tollgate
    ^                                       |
    +-----------------(9002)----------------+   ACK/NAK + window statistics (since v0.2)
```

## Interface contract (fixed - the demo instructions depend on it)

* Binaries in `bin/`: `bin/vehicle`, `bin/channel`, `bin/tollgate`, `bin/bench`.
* UDP ports on 127.0.0.1: vehicle -> channel 9000, channel -> tollgate 9001,
  tollgate -> vehicle feedback 9002.
* Common flags (accepted by every binary):
  `--code {hamming74,hamming1511,secded84,bch157,bch3116,rs,ldpc}` (`none` = uncoded baseline;
  `ldpc` since v0.4) `--profile {toll,hospital}` `--model {bsc,ge,burst}` `--p <float>`
  `--seed <int>` `--count <int>` `--delay-ms <int>`
* Make targets: `make` (build all), `make test`, `make demo` (toll profile), `make demo-hospital`,
  `make compare` (same seed, hamming74 vs bch157, summaries side by side), `make bench`,
  `make plots`, `make sanitize`, `make clean`; demos `demo-arq`, `demo-harq`, `demo-adaptive`,
  `dashboard`.
* `make demo` runs all three processes from ONE terminal (tollgate first, then channel, then
  vehicle, output prefixed `[VEHICLE]`/`[CHANNEL]`/`[TOLLGATE]`), stops cleanly and prints a
  summary. The same binaries also work started by hand in three terminals (see `docs/DEMO.md`).
* `requirements.txt` (matplotlib). `tools/plot.py` writes PNGs to `results/`.
* Must build and run on Ubuntu (incl. WSL) and macOS with only gcc/clang, make, python3.

## Folder layout

```
include/      public headers of the library (one per module)
src/          library modules (bits, crc32, hamming, secded, gf, bch, rs, ldpc, interleave, rng,
              noise, profiles, frame, fec, packet, receiver, sender, adapt, linksim, stats, ...)
              -> build/libfeclink.a
src/apps/     vehicle.c, channel.c, tollgate.c  -> bin/
tools/        bench.c (-> bin/bench), plot.py, demo.sh, compare.sh, dashboard.py/.sh,
              header_experiment.sh, check_docs.sh
tests/        testlib.h, test_<module>.c (unit tests), test_integration.sh, test_failure.sh,
              test_dashboard.py, inject.c (failure-injection helper)
results/      final plots (*.png tracked); CSVs and results/tmp/ are git-ignored
docs/         demo guide, report, algorithms, design decisions, status, roadmap, history
bin/          build output (ignored, only .gitkeep tracked)
```

## Build / test commands

```
make                 # build all binaries into bin/
make test            # unit tests + integration + failure-injection + dashboard tests
make clean
make CC=clang test   # check the other compiler too
make sanitize        # all tests under AddressSanitizer + UBSan (build/asan), must report no findings
sh tools/check_docs.sh   # every relative link and image path in README.md and docs/*.md exists
```

## Coding rules

* C11 (`-std=c11 -Wall -Wextra -Wpedantic`, zero warnings), no external C libraries:
  every code, the CRC, the RNG and GF arithmetic are implemented here.
* POSIX only where needed (sockets, clock_gettime, nanosleep, sigaction) - must compile on macOS.
* **Tests before implementation** for every module (`tests/test_<module>.c`).
* Small functions. Comment the maths clearly - every formula must be explainable in a presentation
  (see `docs/ALGORITHMS.md`).
* Bits are stored one-per-`uint8_t` (values 0/1) inside the coding layer: slow but easy to explain.
  Packing to bytes happens only at the network boundary (`bits_pack`/`bits_unpack`).
* Everything random goes through the seeded RNG in `include/rng.h` - runs must be reproducible.
* Never commit build outputs, binaries, logs or large CSVs. Python is only for plots/dashboard.
* Record every direction-changing choice in `docs/DESIGN_DECISIONS.md` as a new `DD-NN` row:
  decision, alternative considered, rationale, evidence.
* Commit messages: `<area>: <summary>` (historic commits use `phaseN:`; see `docs/DEVELOPMENT.md`).
