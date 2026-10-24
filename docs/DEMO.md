# Demo guide

This is the single guide for setting up, running and presenting the project.
Sections 0.x are one-time setup; sections 1-5 are the demos; then troubleshooting and a
2-minute talking script.
For how the architecture changed between versions and what failed, see the
[Architecture history](ARCHITECTURE_HISTORY.md); the formulas behind every code are in [ALGORITHMS.md](ALGORITHMS.md).

## 0.1 One-time setup (per laptop)

**Windows (via WSL)** - everything in this guide (`make`, `gcc`, `python3`, the demos) runs inside
the **Ubuntu (WSL) terminal**, never in native PowerShell or cmd, which have no gcc/make/python.
PowerShell is only used for the one-time install below, in an *administrator* PowerShell:

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
make test       # unit tests + integration tests; "Integration test passed." appears part-way
                # through the output, the very last line is "dashboard tests passed"
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

> Whole viva demo in one go (preflight, 3-process + tshark, adaptive, dashboard, compare, plots): `tools/present.sh` (`--nopause`, `--check`).

```sh
make demo                      # TOLL_PLAZA profile, Hamming(7,4), 20 frames, 250 ms apart
make demo-hospital             # HOSPITAL_IMAGING profile (bursty Gilbert-Elliott channel)
make demo CODE=bch157          # any code: none hamming74 hamming1511 secded84 bch157 bch3116 rs ldpc
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

### Retransmission strategies (since v0.2)

```sh
make demo-arq                  # Stop-and-Wait ARQ: uncoded + CRC-32, retransmit on NAK/timeout
make demo-harq                 # Hybrid ARQ on the HOSPITAL profile (bursts, so retransmissions show):
                               #   Hamming(7,4) first, retransmit only if the CRC fails
make demo-harq CODE=bch157
make demo-arq SHOWBITS=1       # these two print no per-frame bit maps by default; SHOWBITS=1 adds them
# failure injection: the tollgate drops 30% of its ACK/NAKs -> vehicle timeouts, no lost frames
sh tools/demo.sh --strategy harq --code hamming74 --fb-drop 0.3 --count 20 --delay-ms 250
```

(`SHOWBITS=0` also works for `make demo` / `make demo-hospital`; `tools/demo.sh` reads the
`SHOWBITS` environment variable, default 1.) The live demos use only 20 frames, so their
numbers differ from the 5000-frame benchmark in `results/bench_strategy.csv`: quote the benchmark.

The vehicle prints `NAK`, `timeout`, `retransmission #k` and `ACK after n transmission(s)`;
the tollgate prints `-> ACK/NAK` after each frame and `seq=5#1` for retransmission 1 of seq 5.
Extra flags: `--rtt-ms` (simulated round trip, default 20), `--timeout-ms` (100),
`--max-retries` (4), `--fb-drop` (0). With `--strategy fec` (default) nothing changes.

Header corruption (failure injection, breaks design record DD-01 on purpose): the channel flips 1-4
random bits in the 16-byte header of 5% of the packets. Receivers log `ignoring malformed packet`
or `dropping frame ... unknown code/size/strategy` and keep running; the channel prints
`HEADER CORRUPTED`. `sh tools/header_experiment.sh` measures the cost (see [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md), DD-01).

```sh
sh tools/demo.sh --code rs --corrupt-header 0.05 --count 100 --delay-ms 50
```

### Adaptive FEC (since v0.2, the highlight)

```sh
make demo-adaptive             # schedule toll:60,hospital:80,toll:100, 240 frames, ~14 s
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

### Live dashboard (since v0.2)

```sh
make dashboard                 # prints: open http://127.0.0.1:8050/  then runs the adaptive demo
                               # at 120 ms/frame: ~33 s of demo after a 3 s start
PORT=8080 make dashboard       # other port
```

`make dashboard` starts `tools/dashboard.py` (Python standard library only, no pip needed) and,
3 s later, the adaptive demo with the tollgate writing a JSON-lines live log
(`--jsonl results/tmp/live.jsonl`, one line per transmission). Open the URL on the projector:
counters (CORRECT / DETECTED_FAIL / SILENT_WRONG, retransmissions, decode latency), the last 40
transmissions (bits flipped vs bits corrected, failed frames marked), and the code level over time
on top of the channel profile. The page follows the OS light/dark setting and refreshes twice a
second. After the demo the dashboard keeps showing the final state until Ctrl-C.
The page reads its data from `http://127.0.0.1:8050/api/state` (JSON). The dashboard is deliberately
slow (120 ms/frame) so people can watch it; `make demo-adaptive` runs the same scenario in the
terminal in ~14 s (40 ms/frame) and is the faster alternative when time is short.
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
tollgate -> vehicle **9002** (ACK/NAK feedback for `arq`, `harq` and `adaptive`). Change them with
`--channel-port`, `--tollgate-port`, `--feedback-port` (give the same values to all three).

### 2.1 Wireshark proof (the frames really cross the network)

Capture the section 2 run to show that the three programs exchange UDP packets over the loopback
interface, and that the channel changes bits between 9000 and 9001.

Install (Ubuntu, Debian, WSL):

```sh
sudo apt install -y wireshark tshark
```

During the install, answer **Yes** to "Should non-superusers be able to capture packets?". To capture
without `sudo`, add yourself to the group and log in again:

```sh
sudo usermod -aG wireshark $USER      # then log out and back in
```

**WSL2:** the Wireshark window needs WSLg (Windows 11). Without it, use the `tshark` commands below.

**Start the capture before the demo** (packets sent earlier are not captured):

GUI: open Wireshark, double-click `Loopback: lo` (on macOS: `lo0`), and enter this display filter:

```
udp.port == 9000 || udp.port == 9001 || udp.port == 9002
```

CLI: run this in a fourth terminal, then the three commands of section 2:

```sh
mkdir -p results/tmp
sudo tshark -i lo -f "udp portrange 9000-9002" -w results/tmp/wire.pcap -a duration:30
```

When it has stopped (or after Ctrl-C), make the file readable and count the packets per port:

```sh
sudo chown $USER results/tmp/wire.pcap        # tshark run as root writes a root-only file
tshark -r results/tmp/wire.pcap -T fields -e udp.dstport | sort | uniq -c
```

Expected counts: with the default `--strategy fec` the 9000 and 9001 counts are equal and there are
no 9002 packets. With `--strategy arq`, `harq` or `adaptive` there is one 9002 packet per data packet
on 9000, each an ACK or a NAK.

What to look at:

| port | what you see |
|---|---|
| 9000, vehicle -> channel | 16-byte header, then the coded bits. No sidecar. For `--code hamming74`: UDP length 52 |
| 9001, channel -> tollgate | the same header, except byte 7 bit 0 is set. Coded bits with some bits flipped. The sidecar is appended: UDP length 96 |
| 9002, tollgate -> vehicle | 22 bytes, starting with `FB`. Byte 3: `1` = ACK, `2` = NAK |

Bits flipped by the channel show up as bytes that differ between the 9000 and 9001 packet of the same
frame (same `seq` and `attempt`). Summed over a run, the differing bits equal the flip count the channel
writes into the 9001 sidecar (2 bytes right after the coded bits). The sidecar is for evaluation only:
the tollgate never reads it to decode.

Packet layout, from `include/packet.h` and `src/feedback.c`:

| offset | bytes | field |
|---|---|---|
| 0 | 2 | magic `FL` (feedback packets: `FB`) |
| 3 | 1 | type: 1 = DATA, 2 = END (feedback: 1 = ACK, 2 = NAK) |
| 7 | 1 | flags, bit 0 = sidecar present (9001 only) |
| 8 | 4 | `seq`, big-endian (feedback: 4-7) |
| 12 | 2 | `nbits`, number of coded bits, big-endian |
| 14 | 1 | `attempt`: 0 = first transmission, 1.. = retransmission (feedback: byte 8) |
| 15 | 1 | strategy: 0 fec, 1 arq, 2 harq, 3 adaptive |
| 16 | B | coded bits, packed MSB first; the only part the channel corrupts |

Feedback packets are 22 bytes; bytes 9 onwards carry the code level and the window statistics.

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
| `--code` | `none`, `hamming74`, `hamming1511`, `secded84`, `bch157`, `bch3116`, `rs`, `ldpc` |
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
make bench                      # ~1 min; writes the CSVs below (all git-ignored, plots are tracked)
pip install -r requirements.txt # inside the venv (section 0.4)
make plots                      # CSVs -> results/*.png
```

`make bench` (see the `bench:` rule in the Makefile; `FRAMES=5000 SEED=42 SEEDS=20`) writes:

| file | content |
|---|---|
| `results/bench.csv`, `bench_hospital.csv` | every code vs BSC / Gilbert-Elliott / burst sweeps, toll and hospital |
| `results/bench_ldpc.csv`, `bench_ldpc_hospital.csv` | BCH, RS and LDPC compared |
| `results/bench_strategy.csv`, `adaptive_trace.csv` | fec / arq / harq / adaptive on toll, hospital and the toll-hospital-toll schedule; level over time |
| `results/bench_rtt_5.csv`, `_20.csv`, `_50.csv` | the same strategy bench at 5 / 20 / 50 ms round trip |
| `results/bench_strategy_seeds.csv` (+ `_raw`, `_diff`) | 20 seeds, mean and 95% CI, paired differences |
| `results/bench_ladder_rs_seeds.csv` (+ `_raw`, `_diff`) | same for the all-Reed-Solomon ladder |

`make plots` turns these into about 26 PNGs in `results/` (already committed, so you can show them
without running anything): per CSV set (`bench`, `_hospital`, `_ldpc`, `_ldpc_hospital`)
`success_vs_p_bsc`, `success_vs_pgb_ge`, `success_vs_burst_len`, `silent_wrong`, `latency`; plus
`strategy_comparison`, `goodput_vs_success`, `goodput_vs_success_ci`, `adaptive_level`,
`rtt_sensitivity` and `ladder_rs_experiment`.

## 6. Troubleshooting

| symptom | fix |
|---|---|
| `make: command not found` / `gcc: not found` / `cc: not found` | redo section 0.1 (`build-essential` on Ubuntu/WSL, `xcode-select --install` on macOS) |
| `cannot bind UDP 127.0.0.1:9000: Address already in use` | an old run is still alive. List it: `pgrep -fl "bin/(vehicle|channel|tollgate)"`, stop it by PID: `kill <pid>` (only those PIDs - do not `kill -9` things you do not recognise). Or use other ports: add `--channel-port 9100 --tollgate-port 9101 --feedback-port 9102` to all three programs |
| three-terminal demo: nothing arrives at the tollgate | start order matters: **tollgate first, then channel, then vehicle**; all three need the same port flags |
| Wireshark / tshark shows no packets | the capture was started after the demo, or on the wrong interface: start the capture first and use `lo` (macOS: `lo0`) |
| `permission denied` or no capture interfaces | run with `sudo`, or answer **Yes** at the install prompt and run `sudo usermod -aG wireshark $USER`, then log out and back in |
| garbled characters like `^[[32m` | the terminal does not support colours: add `--no-color` (e.g. `sh tools/demo.sh --no-color ...`) |
| `matplotlib is missing` from `make plots` | activate the venv (`source .venv/bin/activate`) and `pip install -r requirements.txt` |
| dashboard: `Address already in use` on 8050 | `PORT=8080 make dashboard` (then open `http://127.0.0.1:8080/`) |
| a test fails right before the presentation | do **not** fix code live. Demo the parts that pass (`make demo`, `make compare`, the plots in `results/` are committed) and mention the failing test honestly |

## 7. Two-minute talking script

Each line: what to say, and what to run or show while saying it. About 230 spoken words (228 counted), roughly
2 minutes at 130 words per minute. Every number comes from `results/bench.csv`,
`results/bench_hospital.csv` or `results/bench_strategy.csv` (`make bench`), or from
`make compare` (seed 42, 1000 frames). The 20-frame live demos show different numbers than the
5000-frame benchmark, so the retransmission row uses the plot instead of a live demo.

| # | say | run / show |
|---|---|---|
| 1 | "A car's RFID tag ID must reach the toll gantry in one pass. Near trucks, or near MRI and surgical equipment, the radio link gets bursts of bit errors. Retransmitting costs time the car does not have, so we correct errors in place." | README architecture diagram |
| 2 | "Hamming(7,4) fixes one error per block. With two errors in a block it fixes the wrong bit, silently." | `make demo-hospital` - point at a magenta `M` in the bit map (seed 42 shows several; the toll demo has none) |
| 3 | "So every ID carries a CRC-32. Across 310,000 test frames per code, a wrong ID was never accepted." | `results/silent_wrong.png` |
| 4 | "BCH codes fix several errors per block. Same seed, same noise: Hamming 93.1% correct, BCH(15,7) 99.2%." | `make compare` |
| 5 | "In the hospital, errors come in bursts. Reed-Solomon works on bytes, so a burst damages only a few symbols. At code rate 0.67 it delivers 83% with no interleaver; Hamming with an interleaver manages 61%." | `results/success_vs_burst_len_hospital.png` |
| 6 | "Plain retransmission, ARQ, needs 1.78 extra transmissions per frame, a 94 ms worst case, and still delivers only 82%. HARQ with Reed-Solomon: 0.09 retransmissions, 32 ms, 100%." | `results/strategy_comparison.png` (5000 frames; do not run the 20-frame demos here) |
| 7 | "Our adaptive controller climbs the code ladder when hospital interference starts, and steps back down when it ends." | `make demo-adaptive` (~14 s; point at `LEVEL UP` / `LEVEL DOWN`), or `make dashboard` |
| 8 | "Is it worth it? Only partly. Adaptive delivers 99.94% of IDs with a 33 ms worst case; goodput 0.505 against 0.490 for always using the strongest code. Plain HARQ with Reed-Solomon t=4 does best: 100% at goodput 0.61. Adaptation only pays if the lower rungs have higher code rates, and ours do not." | `results/goodput_vs_success.png`, `results/adaptive_level.png` |
