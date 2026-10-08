# CLAUDE.md - working instructions for this repository

**Read `PROGRESS.md` first** - it says which phase is done, what is broken and the exact next step.
Then skim `PLAN.md` (all phases + week-2 backlog) and `DECISIONS.md` (design choices; lines starting
with 🔀 changed the project's direction and should be reviewed by the team).

**After every task: `make test`, check `git status` (no binaries/logs/CSVs staged), commit, push.**
Commit messages for phases: `phaseN: <summary>`.

## Project

"Adaptive Multi-Error FEC Link Layer for High-Interference Environments" - a university Computer
Networks project that extends the approved project "Fast-Lane FEC for Automated Toll Booths".
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
    +------------(9002, week 2)-------------+   feedback (reserved)
```

## FIXED INTERFACE (do not rename - the demo instructions depend on it)

* Binaries in `bin/`: `bin/vehicle`, `bin/channel`, `bin/tollgate`, `bin/bench`.
* UDP ports on 127.0.0.1: vehicle -> channel 9000, channel -> tollgate 9001,
  tollgate -> vehicle feedback 9002 (reserved for week 2).
* Common flags (accepted by every binary):
  `--code {hamming74,hamming1511,secded84,bch157,bch3116,rs}`
  `--profile {toll,hospital}` `--model {bsc,ge,burst}` `--p <float>` `--seed <int>`
  `--count <int>` `--delay-ms <int>`
* Make targets: `make` (build all), `make test`, `make demo` (toll profile), `make demo-hospital`,
  `make compare` (same seed, hamming74 vs bch157, summaries side by side), `make bench`,
  `make plots`, `make clean`.
* `make demo` runs all three processes from ONE terminal (tollgate first, then channel, then vehicle,
  output prefixed `[VEHICLE]`/`[CHANNEL]`/`[TOLLGATE]`), stops cleanly and prints a summary.
  The same binaries also work started by hand in three terminals (see `docs/DEMO.md`).
* `requirements.txt` (matplotlib). `tools/plot.py` writes PNGs to `results/`.
* Must build and run on Ubuntu (incl. WSL) and macOS with only gcc/clang, make, python3.

## Folder layout

```
include/      public headers of the library (one per module)
src/          library modules (bits, crc32, hamming, secded, gf, bch, rs, interleave, rng,
              noise, profiles, frame, codec, net, term, cli ...) -> build/libfeclink.a
src/apps/     vehicle.c, channel.c, tollgate.c  -> bin/
tools/        bench.c (-> bin/bench), plot.py, demo.sh, compare.sh
tests/        testlib.h, test_<module>.c (unit tests), test_integration.sh
results/      final plots (*.png tracked); CSVs and results/tmp/ are ignored
docs/         DEMO.md and other documentation
bin/          build output (ignored, only .gitkeep tracked)
```

## Build / test commands

```
make            # build all binaries into bin/
make test       # unit tests + integration test (must pass before every commit)
make clean
make CC=clang test   # check the other compiler too
```

## Coding rules

* C11 (`-std=c11 -Wall -Wextra -Wpedantic`, zero warnings), no external C libraries:
  every code, the CRC, the RNG and GF arithmetic are implemented here.
* POSIX only where needed (sockets, clock_gettime, nanosleep, sigaction) - must compile on macOS.
* **Tests before implementation** for every module (`tests/test_<module>.c`).
* Small functions. Comment the maths clearly - students must present and explain it.
* Bits are stored one-per-`uint8_t` (values 0/1) inside the coding layer: slow but easy to explain.
  Packing to bytes happens only at the network boundary (`bits_pack`/`bits_unpack`).
* Everything random goes through the seeded RNG in `include/rng.h` - runs must be reproducible.
* Never commit build outputs, binaries, logs or large CSVs. Python is only for plots/dashboard.
* Log direction-changing choices in `DECISIONS.md` as `🔀 chose X over Y because Z`.
