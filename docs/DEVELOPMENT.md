# Development guide

How to set up, build, test and extend the project, and how branches and releases are managed.
Coding rules and the fixed interface are summarised here and specified in
[CONTRIBUTING.md](../CONTRIBUTING.md); the current state is in [STATUS.md](STATUS.md).

## Environment

| tool | needed for | notes |
|---|---|---|
| C11 compiler: gcc or clang | everything | `cc` by default; `make CC=clang` for clang |
| GNU make 3.81+ | building, tests, demos | macOS ships 3.81, Ubuntu 4.x |
| POSIX `sh`, `awk` | demo, integration and failure-injection scripts | |
| python3 (standard library) | `tests/test_dashboard.py`, `tools/dashboard.py` | skipped by `make test` if python3 is missing |
| matplotlib (`requirements.txt`) | `make plots` and the plot smoke test | optional; in a venv: `python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt` |
| ASan/UBSan runtime | `make sanitize` | part of gcc on Ubuntu; clang needs `libclang-rt-<ver>-dev` |

Supported platforms: Ubuntu (incl. WSL) and macOS. Per-OS installation commands are in
[DEMO.md](DEMO.md) section 0.1. The code uses only C11 plus POSIX where needed (sockets, `poll`,
`clock_gettime`, `nanosleep`, `sigaction`).

## Build

```sh
make                 # library build/libfeclink.a + bin/vehicle bin/channel bin/tollgate bin/bench
make CC=clang        # same with clang
make clean           # remove build/, bin/* and results/tmp/
```

Flags: `-std=c11 -Wall -Wextra -Wpedantic` (zero warnings), `-O2 -g` by default (override
through the environment: `CFLAGS=-O0 make`). All modules of `src/` go into one static library
so the UDP programs and the in-process benchmark run exactly the same code.

## Test matrix

| command | what runs | where | expected result |
|---|---|---|---|
| `make test` | 24 unit-test binaries (`tests/test_*.c`), `tests/test_integration.sh` (real three-process UDP runs, ports 19100-19102), `tests/test_failure.sh` (failure injection, ports 19200-19202), `tests/test_dashboard.py` | every commit, locally and in CI | last lines "Integration test passed.", "Failure-injection test passed.", "dashboard tests passed" |
| `make CC=clang test` | same with clang | before a release | same |
| `make sanitize` | every unit and integration test built with `-fsanitize=address,undefined` in `build/asan`; any ASan/LSan/UBSan report fails the target | before a release, and in CI | "make sanitize: all tests passed under ASan + UBSan, no findings." |
| `sh tools/check_docs.sh` | every relative link and image path in `README.md`, `CONTRIBUTING.md` and `docs/*.md` resolves | after editing documentation | "check_docs: all N links resolve" |
| GitHub Actions (`.github/workflows/ci.yml`) | `make` + `make test` on ubuntu-latest and macos-latest, `make sanitize` on ubuntu-latest | every push and pull request | 3 green jobs |
| `make bench` (+ `make plots`) | regenerates `results/*.csv` (and PNGs) | when results are updated | 1-2 minutes, host-dependent; success / retransmission columns are bit-reproducible, latency columns vary by microseconds |

`tests/test_cli.c` prints 6 expected error messages (bad-flag tests); they are not failures.

## Coding rules

* **C11, zero warnings** with `-Wall -Wextra -Wpedantic`, gcc and clang.
* **No external C libraries**: every code, the CRC, the RNG and the GF arithmetic are in `src/`.
* **Tests first**: a module `src/<m>.c` gets `tests/test_<m>.c` before the implementation;
  tests use the small `tests/testlib.h` (`TEST`, `CHECK`, `RUN`).
* **Seeded RNG only** (`include/rng.h`, xoshiro256** + splitmix64); every random choice derives
  from `--seed`, so runs are reproducible across platforms.
* **Bits one per `uint8_t`** inside the coding layer; packing only at the network boundary.
* **Small functions, commented maths**: the formula a function implements is written next to it
  (and collected in [ALGORITHMS.md](ALGORITHMS.md)).
* **No build outputs, binaries, logs or CSVs in git**; `results/*.png` are tracked final plots.
* **Direction-changing choices** become a new `DD-NN` record in [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md).

## Directory layout

```
include/            public headers, one per library module
src/                library modules -> build/libfeclink.a
  apps/             vehicle.c, channel.c, tollgate.c -> bin/
tests/              testlib.h, test_<module>.c, test_integration.sh, test_failure.sh,
                    test_dashboard.py, inject.c (malformed-packet sender for test_failure.sh)
tools/              bench.c (-> bin/bench), plot.py, demo.sh, compare.sh, dashboard.py, dashboard.sh,
                    header_experiment.sh, check_docs.sh
results/            tracked PNGs; CSVs and results/tmp/ are git-ignored
docs/               DEMO, REPORT, ALGORITHMS, DESIGN_DECISIONS, ARCHITECTURE_HISTORY, STATUS,
                    ROADMAP, DEVELOPMENT
.github/workflows/  ci.yml
build/, bin/        build outputs (ignored; bin/.gitkeep tracked)
```

## Adding a new code to the registry

The FEC layer (`src/fec.c`) treats every code as a block code with the same adapter signature,
so a new code touches a small, fixed set of places (LDPC in v0.4 is the worked example):

1. **Tests first**: `tests/test_<code>.c` - encode/decode round trip, every error pattern the
   code guarantees to correct (exhaustively where feasible), detected failures beyond that.
2. **Module**: `include/<code>.h` + `src/<code>.c` with `..._encode(data -> code)` and
   `..._decode(code -> data)` returning the number of corrected bits (>= 0) or a negative value
   for a detected failure; on failure output the *received* data bits (the RS fix in v0.3
   shows why: never leave the output uninitialized).
3. **Code id**: add `CODE_<NAME> = <next id>` to `code_id_t` in `include/fec.h` (the id travels
   in the packet header, so never reuse or renumber an existing id).
4. **Registry entry**: two adapters `<name>_enc` / `<name>_dec` and one line in the `CODES[]`
   table of `src/fec.c`: `{{CODE_<NAME>, "<cli-name>", "<Label(n,k)>", n, k, t}, enc, dec}`.
   If the code needs one-time setup, build it in `fec_config_valid` (see `CODE_LDPC`) so the first
   decode is not charged for it.
5. **CLI and plots**: add the name to the `--code {...}` help line in `src/cli.c` and to
   `CODE_ORDER` / markers / labels in `tools/plot.py`; extend `tests/test_fec.c`.
6. **Benchmark**: decide whether it joins the default sweep (`tools/bench.c`) - joining changes
   `results/bench.csv`; v0.4 kept LDPC out and used `--compare-codes` instead (DD-29).
7. **Docs**: README feature/results tables, [ALGORITHMS.md](ALGORITHMS.md) (formula + worked
   example), a DD record if it changes the project's direction.

The receive path (`src/receiver.c`), the packet validation (`src/packet.c`) and the adaptive
ladder (`src/adapt.c`) work through the registry and need no change unless the code should be on
the ladder.

## Branch naming

| prefix | use | example |
|---|---|---|
| `main` | always green, released state; updated by fast-forward only | |
| `release/vX.Y-<topic>` | the branch a version was developed on | `release/v0.2-arq-adaptive-dashboard` |
| `feature/<topic>` | a feature or experiment | `feature/ldpc-code` |
| `docs/<topic>` | documentation only | `docs/demo-guide-fixes` |
| `chore/<topic>` | maintenance, verification passes, cleanup | `chore/verify-report-numbers` |

Topics are lower-case words joined by `-`. The map from the original working names to these
names is in [ARCHITECTURE_HISTORY.md](ARCHITECTURE_HISTORY.md#branch-and-tag-map). Commit
messages are `<area>: <summary>` (historic commits use `phaseN:`, `cleanup-N:` etc.).

## Release process

1. Work on a branch named by the convention above.
2. `make test`, `make CC=clang test`, `make sanitize` and `sh tools/check_docs.sh` pass locally;
   `git status` shows no build outputs, logs or CSVs.
3. If results changed: `make bench`, check every number quoted in `README.md`, `docs/REPORT.md`
   and `docs/DESIGN_DECISIONS.md` against the regenerated CSVs (a verification pass), then
   `make plots` and commit the PNGs you want to publish.
4. Update [STATUS.md](STATUS.md), [ROADMAP.md](ROADMAP.md) and the version section of
   [ARCHITECTURE_HISTORY.md](ARCHITECTURE_HISTORY.md).
5. Fast-forward `main` to the branch (`git merge --ff-only`), push, wait for CI to be green.
6. Tag the release commit with an annotated tag and push it:
   `git tag -a vX.Y.Z -m "vX.Y.Z: <summary>" <sha> && git push origin vX.Y.Z`.

History is never rewritten and `main` is never force-pushed.

## Open problems

* **CRC burst guarantee in transmission order.** The CRC-32 is computed reflected (LSB-first per
  byte, standard IEEE 802.3) while the payload bits are sent MSB-first and the CRC is appended
  big-endian. In transmission order the textbook "every burst <= 32 bits is detected" therefore
  does not hold exactly: 54 of 97 32-bit windows of the payload have full rank, and the shortest
  undetected burst is 30 bits (weight 18) - see [ALGORITHMS.md](ALGORITHMS.md#2-crc-32). Every 1-,
  2- and 3-bit error is detected and SILENT_WRONG was 0 in every run. Fix options (wire-format
  change, new header version): send each byte LSB-first, or use a non-reflected CRC whose bit
  order matches the transmission order; then re-run `make bench`.
* **Header corruption -> misrouting** (v0.3): protect the header with its own CRC-16; see
  [STATUS.md](STATUS.md#open-problems).
* **Talking script numbers** ([DEMO.md](DEMO.md) section 7, rows 6 and 8) are single-seed values
  while README uses 20-seed means (ARCHITECTURE_HISTORY D.5); left for the presenters to decide.
* **`make sanitize CC=clang`** has not been run (clang's ASan runtime was missing in the build
  container); macOS is covered by CI only.
* **CLI help text** (`src/cli.c`) still labels the strategy flags "week 2"; it is program output,
  so it was left unchanged in the documentation-only cleanup (changing it is a one-line, behaviour-
  visible edit for a future release).
