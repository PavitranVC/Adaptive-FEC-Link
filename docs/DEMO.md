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

### Retransmission strategies (week 2)

```sh
make demo-arq                  # Stop-and-Wait ARQ: uncoded + CRC-32, retransmit on NAK/timeout
make demo-harq                 # Hybrid ARQ: Hamming(7,4) first, retransmit only if the CRC fails
make demo-harq CODE=bch157
# failure injection: the tollgate drops 30% of its ACK/NAKs -> vehicle timeouts, no lost frames
sh tools/demo.sh --strategy harq --code hamming74 --fb-drop 0.3 --count 20 --delay-ms 250
```

The vehicle prints `NAK`, `timeout`, `retransmission #k` and `ACK after n transmission(s)`;
the tollgate prints `-> ACK/NAK` after each frame and `seq=5#1` for retransmission 1 of seq 5.
Extra flags: `--rtt-ms` (simulated round trip, default 20), `--timeout-ms` (100),
`--max-retries` (4), `--fb-drop` (0). With `--strategy fec` (default) nothing changes.

### Adaptive FEC (week 2, the highlight)

```sh
make demo-adaptive             # schedule toll:60,hospital:80,toll:100, 240 frames, ~15 s
make demo-adaptive ASCHED=toll:100,hospital:150,toll:150 DELAY=60
```

The channel announces `=== interference changes: HOSPITAL_IMAGING ... ===`; the vehicle prints
every decision in bold, e.g.

```
[VEHICLE] *** LEVEL UP   -> L3 BCH(31,16)   (window failure rate 62% > up-threshold 20%) ***
[VEHICLE] *** LEVEL DOWN -> L4 RS(24,16) t=4   (BER^ 0.0095, predicted failure 2.8% < down-threshold 5%) ***
```

Flags (give the same ones to all three programs): `--window` (32; the demo uses 16 so it reacts
within seconds), `--up-threshold` (0.20), `--down-threshold` (0.05), `--start-level` (1),
`--schedule profile:transmissions,...` (channel). Ladder: L0 uncoded, L1 Hamming(7,4),
L2 BCH(15,7), L3 BCH(31,16), L4 RS t=4, L5 RS t=8. HARQ retransmits on CRC failure.

### Live dashboard (week 2)

```sh
make dashboard                 # prints: open http://127.0.0.1:8050/  then runs the adaptive demo
PORT=8080 make dashboard       # other port
```

`make dashboard` starts `tools/dashboard.py` (Python standard library only, no pip needed) and,
3 s later, the adaptive demo with the tollgate writing a JSON-lines live log
(`--jsonl results/tmp/live.jsonl`, one line per transmission). Open the URL on the projector:
counters (CORRECT / DETECTED_FAIL / SILENT_WRONG, retransmissions, decode latency), the last 40
transmissions (bits flipped vs bits corrected, failed frames marked), and the code level over time
on top of the channel profile. The page follows the OS light/dark setting and refreshes twice a
second. After the demo the dashboard keeps showing the final state until Ctrl-C.
Any run can feed it: `./bin/tollgate ... --jsonl FILE` + `python3 tools/dashboard.py --log FILE`.

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
make bench                      # results/bench.csv (+ bench_hospital.csv, bench_strategy.csv)
./bin/bench --strategies --schedule toll:2000,hospital:2000,toll:2000 --frames 5000 \
    --trace results/adaptive_trace.csv --out results/bench_strategy.csv  # fec/arq/harq/adaptive
pip install -r requirements.txt
make plots                      # results/*.png incl. strategy_comparison.png, adaptive_level.png
```
