# Demo guide

All commands are run from the repository root after `make`.
Works on Ubuntu, WSL and macOS (only `cc`/`gcc`/`clang`, `make`, `sh`, `python3` needed).

## 1. One-terminal demo (recommended for presenting)

```sh
make demo                      # TOLL_PLAZA profile, Hamming(7,4), 20 frames, 250 ms apart
make demo-hospital             # HOSPITAL_IMAGING profile (bursty Gilbert-Elliott channel)
make demo CODE=bch157          # any code: none hamming74 hamming1511 secded84 bch157 bch3116 rs
make demo COUNT=50 DELAY=100 SEED=7
```

`make demo` starts **tollgate first, then channel, then vehicle** in the background of one
terminal (`tools/demo.sh`). Every line is prefixed `[VEHICLE]`, `[CHANNEL]` or `[TOLLGATE]`.
When the vehicle has sent all frames it sends an END packet; channel and tollgate stop by
themselves and the tollgate prints the **summary** (CORRECT / DETECTED_FAIL / SILENT_WRONG,
bits flipped/corrected, decode latency). Ctrl-C stops everything cleanly (summary still printed).

The tollgate's coloured **bit map** (`--show-bits`, on in the demo) shows each damaged frame,
grouped per code block:

| colour (with `--no-color`) | meaning |
|---|---|
| green background (`C`)  | bit flipped by the channel and repaired by the decoder |
| red background (`X`)    | bit flipped by the channel and NOT repaired |
| magenta background (`M`)| bit NOT flipped but changed by the decoder (miscorrection) |
| dim (`.`)               | untouched |

Equivalent direct call with any flags:

```sh
sh tools/demo.sh --profile hospital --model burst --burst-len 12 --code secded84 --count 30 --delay-ms 300
```

Good story for a presentation (same seed, hospital interference):

```sh
make demo-hospital CODE=hamming74          # bursts break Hamming
make demo-hospital CODE=rs                 # Reed-Solomon shrugs them off
sh tools/demo.sh --profile hospital --code bch3116 --interleave 8 --count 20 --delay-ms 250
```

## 2. Three-terminal demo (shows that they really are separate processes)

Use the **same flags** in all three terminals; start them in this order:

```sh
# terminal 1 - receiver (roadside unit)
./bin/tollgate --profile toll --code hamming74 --show-bits

# terminal 2 - noisy link
./bin/channel --profile toll --model bsc --p 0.01 --seed 42

# terminal 3 - sender (vehicle); starts transmitting immediately
./bin/vehicle --profile toll --code hamming74 --seed 42 --count 20 --delay-ms 500
```

Ports (UDP, 127.0.0.1): vehicle -> channel **9000**, channel -> tollgate **9001**,
tollgate -> vehicle **9002** (reserved for week-2 feedback). Change them with
`--channel-port`, `--tollgate-port`, `--feedback-port` (give the same values to all three).

## 3. Side-by-side comparison

```sh
make compare                   # hamming74 vs bch157, same seed, 1000 frames, toll profile
make compare SEED=7 CCOUNT=5000
CODES="hamming74 secded84 bch157 bch3116" sh tools/compare.sh --profile hospital --seed 42 --count 2000
```

Same seed means the same sequence of tag IDs and the same noise process for every code.

## 4. Useful flags

| flag | meaning |
|---|---|
| `--code` | `none`, `hamming74`, `hamming1511`, `secded84`, `bch157`, `bch3116`, `rs` |
| `--profile` | `toll` (TOLL_PLAZA) or `hospital` (HOSPITAL_IMAGING) |
| `--model` | `bsc`, `ge` (Gilbert-Elliott), `burst`; default = profile's model (toll: bsc, hospital: ge) |
| `--p` | bsc: flip probability; ge: P(good->bad) per bit; burst: probability a frame gets a burst |
| `--burst-len` | burst model: burst length L |
| `--seed`, `--count`, `--delay-ms` | reproducibility, number of frames, pacing |
| `--rs-t` | Reed-Solomon correctable bytes (1..16, default 4) |
| `--interleave` | block-interleaver depth (default 1 = off); e.g. 8 for hamming74/bch3116 |
| `--no-color`, `--show-bits`, `--quiet` | output control |
| `--log FILE`, `--summary-file FILE` | per-frame CSV / key=value summary |

## 5. Benchmark and plots

```sh
make bench                      # results/bench.csv (+ results/bench_hospital.csv)
pip install -r requirements.txt
make plots                      # results/*.png
```
