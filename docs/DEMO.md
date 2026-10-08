# Demo guide

This is the single guide for setting up, running and presenting the project.
Sections 0.x are one-time setup; sections 1-5 are the demos; then troubleshooting and a
2-minute talking script.

## 0.1 One-time setup (per laptop)

**Windows (via WSL)** - in an *administrator* PowerShell:

```powershell
wsl --install
```

Restart the PC, open **Ubuntu** from the Start menu (create a user when asked), then in Ubuntu:

```sh
sudo apt update && sudo apt install -y build-essential git python3 python3-pip python3-venv
```

**Ubuntu / Debian** - the same line:

```sh
sudo apt update && sudo apt install -y build-essential git python3 python3-pip python3-venv
```

**macOS** - install the command-line tools (gives `clang` as `gcc`/`cc`, `make` and `git`):

```sh
xcode-select --install
python3 --version      # if missing, install Python 3 from https://www.python.org/downloads/
```

## 0.2 Clone

```sh
git clone https://github.com/PavitranVC/Adaptive-FEC-Link.git
cd Adaptive-FEC-Link          # main is the default branch
```

## 0.3 Build and test

```sh
make            # builds bin/vehicle bin/channel bin/tollgate bin/bench (a few seconds)
make test       # unit tests + integration test; ends with "Integration test passed."
```

## 0.4 Python environment (only for plots and the dashboard)

The C programs need no Python. `make plots` needs matplotlib; the dashboard needs only python3.

```sh
python3 -m venv .venv
source .venv/bin/activate          # run this again in every new terminal
pip install -r requirements.txt
```

Open the results folder (plots are `results/*.png`):

| system | command |
|---|---|
| WSL     | `explorer.exe results` |
| macOS   | `open results` |
| Linux   | `xdg-open results` |

All commands below are run from the repository root after `make`.

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
make plots                      # results/*.png incl. strategy_comparison.png, adaptive_level.png,
                                #   goodput_vs_success.png
```

## 6. Troubleshooting

| symptom | fix |
|---|---|
| `make: command not found` / `gcc: not found` / `cc: not found` | redo section 0.1 (`build-essential` on Ubuntu/WSL, `xcode-select --install` on macOS) |
| `cannot bind UDP 127.0.0.1:9000: Address already in use` | an old run is still alive. List it: `pgrep -fl "bin/(vehicle|channel|tollgate)"`, stop it by PID: `kill <pid>` (only those PIDs - do not `kill -9` things you do not recognise). Or use other ports: add `--channel-port 9100 --tollgate-port 9101 --feedback-port 9102` to all three programs |
| three-terminal demo: nothing arrives at the tollgate | start order matters: **tollgate first, then channel, then vehicle**; all three need the same port flags |
| garbled characters like `^[[32m` | the terminal does not support colours: add `--no-color` (e.g. `sh tools/demo.sh --no-color ...`) |
| `matplotlib is missing` from `make plots` | activate the venv (`source .venv/bin/activate`) and `pip install -r requirements.txt` |
| dashboard: `Address already in use` on 8050 | `PORT=8080 make dashboard` (then open `http://127.0.0.1:8080/`) |
| a test fails right before the presentation | do **not** fix code live. Demo the parts that pass (`make demo`, `make compare`, the plots in `results/` are committed) and mention the failing test honestly |

## 7. Two-minute talking script

Each line: what to say, and what to run or show while saying it. Numbers come from
`results/bench.csv`, `results/bench_hospital.csv` and `results/bench_strategy.csv` (`make bench`).

| # | say | run / show |
|---|---|---|
| 1 | "A car's RFID tag ID must reach the toll gantry in one pass. Near trucks - or near MRI and electrosurgery equipment in a hospital - the radio link gets bursts of bit errors. Retransmitting costs time the car does not have, so we correct errors in place." | README architecture diagram |
| 2 | "Hamming(7,4) fixes any single error per block, but two errors in a block make it 'correct' the wrong bit - silently." | `make demo` - point at a magenta `M` in the bit map, if one shows up |
| 3 | "That is why every ID carries a CRC-32: a mis-corrected frame is always rejected. In about 155,000 test frames per code, SILENT_WRONG never happened once." | `results/silent_wrong.png` |
| 4 | "BCH codes fix several errors per block. Same seed, same noise: Hamming 93.1% correct, BCH(15,7) 99.2%." | `make compare` |
| 5 | "In the hospital profile errors come in bursts. Reed-Solomon works on bytes, so a 16-bit burst touches at most 3 bytes - a few symbol errors it fixes easily. RS(24,16) gets 83% on the hospital channel with no interleaver at code rate 0.67; Hamming+interleaver gets 61%, BCH(31,16)+interleaver ties RS at 84% but needs more parity (rate 0.52) - and for very long 32-bit bursts the interleaved BCH is actually better." | `make demo-hospital CODE=rs`, `results/success_vs_burst_len_hospital.png` |
| 6 | "Classic retransmission (ARQ) needs 1.8 extra transmissions per frame and a 94 ms worst case; FEC plus retransmit-on-failure (HARQ) cuts both." | `make demo-arq`, then `make demo-harq`; `results/strategy_comparison.png` |
| 7 | "Our adaptive controller watches the receiver's error statistics and climbs the code ladder when the hospital interference starts, then steps back down when it ends." | `make demo-adaptive` (point at `LEVEL UP` / `LEVEL DOWN`), or `make dashboard` |
| 8 | "Is adaptation worth it? Honestly: only partly. On the toll -> hospital -> toll channel adaptive delivers 99.94% of IDs with a 33 ms worst case, like always using the strongest code (RS t=8, 100%), but it saves only 3% of the transmitted bits (goodput 0.505 vs 0.490). Plain HARQ with RS t=4 does best here: 100% at goodput 0.61. The lesson: an adaptive ladder only pays off if its lower rungs really have higher code rates - ours (Hamming, BCH) do not." | `results/goodput_vs_success.png`, `results/adaptive_level.png` |
