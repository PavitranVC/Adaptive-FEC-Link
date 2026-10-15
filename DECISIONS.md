# DECISIONS

Lines starting with 🔀 are choices that could change the project's direction - please review them.
Format: what was chosen / the alternative / why.

## Phase 0

- 🔀 The frame header (seq number, code id, code parameters) is **never corrupted** by the channel;
  only the FEC-coded payload is. Alternative: corrupt the header too and protect it with its own
  code. Why: the assignment fixes this; it isolates the comparison of payload codes. A header
  corruption experiment is listed as week-2 failure-injection work.
  **Overnight result (header-corruption experiment, `channel --corrupt-header P`):** a corrupted
  packet gets 1-4 random bit flips (uniform) in its 16-byte header; payload noise is unchanged
  (separate RNG stream); END packets are not corrupted. `sh tools/header_experiment.sh` (real UDP
  pipeline, toll profile, RS t=4, seed 42, 2000 frames, reference run for the true seq -> tag map):

  | strategy | P | headers corrupted | delivered | failed | misrouted | lost | tollgate claims CORRECT |
  |---|---|---|---|---|---|---|---|
  | fec  | 0    | 0   | 1929 | 71 | 0  | 0   | 1929 |
  | fec  | 0.01 | 24  | 1906 | 71 | 3  | 20  | 1909 |
  | fec  | 0.05 | 100 | 1835 | 69 | 13 | 83  | 1845 |
  | fec  | 0.2  | 396 | 1558 | 62 | 51 | 329 | 1599 |
  | harq | 0    | 0   | 2000 | 0  | 0  | 0   | 2000 |
  | harq | 0.01 | 24  | 1997 | 1  | 2  | 0   | 2000 |
  | harq | 0.05 | 108 | 1988 | 2  | 9  | 1   | 2000 |
  | harq | 0.2  | 551 | 1960 | 2  | 28 | 10  | 2003 |

  (`failed` = arrived under its own seq but never CORRECT; with fec that is ordinary payload
  noise, identical to P = 0.) Reading: with plain FEC **almost every corrupted header costs one
  frame**: at P = 0.01, 23 of 24 (20 lost + 3 misrouted = 1.15% of all frames), at P = 0.05,
  96 of 100 (83 lost + 13 misrouted = 4.8%). Most are dropped as malformed/invalid (magic,
  version, type, nbits, code fields); a flipped seq bit *misroutes* the frame (the ID is read,
  but filed under another seq) and can make the tollgate discard the real owner of that seq as
  a "duplicate". HARQ hides most of it (a dropped frame is simply retransmitted): 0.15% / 0.6%
  of frames are lost or misrouted at P = 0.01 / 0.05. But the tollgate **cannot see misrouting**:
  its summary over-reports (2003 "correct" frames out of 2000 sent at P = 0.2). The parts that do
  not depend on the header (decoding, CRC, classification) never crashed. Conclusion: keeping
  the header out of the noise (decision #1) is a real assumption, worth ~1 lost frame per
  corrupted header without retransmission; a real link would protect the header (e.g. its own
  CRC-16 or a strong short code) - listed as a next step, not done (it changes the wire format).
- Library + three thin programs: all coding logic lives in `build/libfeclink.a` so the in-process
  benchmark and the UDP pipeline run exactly the same code.
- Bits are kept one-per-byte inside the coding layer (clarity over speed); packed only on the wire.
- Tiny home-made test helper (`tests/testlib.h`) instead of a framework - no external C libraries.
- `.gitignore` lists `bin/` literally (as required); `bin/.gitkeep` is force-added once.

## Phase 1

- CRC-32 is the standard IEEE 802.3 / zip CRC (reflected 0xEDB88320, init/xorout 0xFFFFFFFF),
  computed bitwise (no table) so the division can be explained line by line. It covers the
  12 ID bytes only; the 4 CRC bytes are appended big-endian -> 128-bit payload.
- Hamming(7,4) and Hamming(15,11) share one generic implementation, parameterised by r
  (n = 2^r - 1), with the textbook layout (parity at power-of-two positions, syndrome = error position).
- Block decoders return the number of corrected bits (>= 0) or a negative value when they
  *detect* an uncorrectable pattern (SECDED double error; later BCH/RS decoding failure).

## Phase 2

- RNG: xoshiro256** seeded by splitmix64, implemented in-repo (not `rand()`), so a given
  `--seed` is bit-identical across Linux/WSL/macOS. Each process derives its own sub-stream
  with `rng_derive(seed, "<role>")`.
- 🔀 Gilbert-Elliott channel state is **kept across frames** (one continuous interference process)
  and starts from the stationary distribution. Alternative: reset to "good" at every frame.
  Why: real interference does not know frame boundaries; resetting would under-estimate bursts.
- 🔀 Burst model = classic "burst of length L": first and last bit of the burst always flip, the
  bits in between flip with probability `density` (0.5 in both profiles), and a frame gets a
  burst with probability `p_frame`. Alternative: flip all L bits. Why: matches the textbook
  definition used for burst-correction guarantees (RS/interleaving) and is less artificial.
- 🔀 Meaning of `--p` per model: BSC -> flip probability p; GE -> p_gb (rate at which bursts
  start; mean burst length 1/p_bg and e_good/e_bad come from the profile); BURST -> probability a
  frame contains a burst (length from the profile, override with `--burst-len`).
  Alternative: one global "BER" knob. Why: each model keeps its physical meaning and the profile
  stays the single source of the other parameters.
- 🔀 Profile numbers (see `include/profiles.h`) are modelling assumptions, not measurements:
  TOLL_PLAZA BSC p=0.01, GE (0.002, 0.5, 0.005, 0.5) ~BER 0.007, burst L=4 in 20% of frames;
  HOSPITAL_IMAGING BSC p=0.02, GE (0.005, 0.08, 0.002, 0.5) ~BER 0.031 with 12.5-bit mean
  bursts, burst L=16 in 50% of frames. Default model: toll -> bsc, hospital -> ge.

## Phase 3

- 🔀 Ground truth travels as an **evaluation-only sidecar** that the channel appends to each
  packet (clean codeword, true tag ID, number of flipped bits, profile, model). The channel gets
  the true ID by decoding the *clean* codeword. Alternative: a separate truth log file joined
  offline by seq. Why: live classification in the tollgate during the demo, no file races; the
  tollgate's decoder never reads the sidecar (only `frame_classify` does).
  **Week-2 integrity check (task 1): PASSED.** Code review of `src/apps/tollgate.c` showed the
  decoder only ever read `p->bits`; the sidecar fed only the bit map, statistics and
  classification. To make this a structural guarantee the receive path was moved into
  `src/receiver.c`: `rx_decode()` takes only the header's code parameters and the received bits
  (it has no parameter through which truth could reach it); `rx_classify()` is the only reader of
  the sidecar. `tests/test_receiver.c` decodes 2100 corrupted frames (all 7 codes, bursty
  channel) with the sidecar present, zeroed, filled with garbage, stripped, and stripped via the
  wire format: decoded ID, CRC result, failure flags, corrected-bit count and repaired codeword
  are bit-identical in every case. No decoder fix was needed.
- 🔀 A frame is DETECTED_FAIL if the CRC fails **or** a block decoder detected an uncorrectable
  pattern (SECDED double error, later BCH/RS failure), even if the CRC happens to pass.
  Alternative: CRC only. Why: a real receiver would also reject a frame its decoder flagged.
- 🔀 Payload is coded as consecutive k-bit chunks (last one zero-padded); `code_rate` is the
  *effective* rate 128 / coded bits (includes padding), e.g. Hamming(15,11) -> 128/180 = 0.711.
- Extra code `none` (uncoded baseline) is accepted besides the six required codes.
- All three programs accept every flag (irrelevant ones ignored), so the same command line
  works for all of them. Ports are named by link: `--channel-port` (9000), `--tollgate-port`
  (9001), `--feedback-port` (9002, reserved).
- Shutdown: vehicle sends END (twice) carrying the number of frames sent; the channel forwards it
  (twice); the tollgate prints the summary (lost = sent - received). Backstops: Ctrl-C (summary
  still printed) and `--idle-timeout-ms` (default 10 s, counted only after the first packet so
  the three-terminal demo can be started slowly).
- With `--delay-ms 0` the vehicle still sleeps 100 us between frames so loopback socket buffers
  never overflow (lost frames would distort the statistics).
- Decode latency = FEC decode + CRC check of one frame, measured with CLOCK_MONOTONIC.

## Phase 4

- One generic binary BCH implementation (`bch_init(m, primitive poly, t)`) builds g(x) as the LCM
  of minimal polynomials from cyclotomic cosets; BCH(15,7) and BCH(31,16) are two instances.
  Primitive polynomials: GF(16) x^4+x+1, GF(32) x^5+x^2+1.
- Decoder = syndromes -> Berlekamp-Massey -> Chien search (no Forney needed: binary errors).
  A pattern is reported as a detected failure when deg Lambda > t or the number of Chien roots
  differs from deg Lambda (it never outputs a non-codeword; tested exhaustively for weight 3).
- Syndromes and Chien search use the straightforward O(n * t) evaluation (no Horner/LFSR tricks)
  so the maths stays visible; latency is still only a few microseconds per frame.

## Phase 5

- The simulation loop lives in the library (`src/sim.c`, unit-tested); `bin/bench` only sweeps
  and writes CSV. Same seed for every point -> every code sees the same tags and noise process.
- 🔀 Swept parameter per model (CSV `param` column): bsc -> p in [0.0005, 0.1]; ge -> p_gb in
  [0.0005, 0.05] with the profile's p_bg/e_good/e_bad; burst -> burst length L in [1, 32] with a
  burst in *every* frame. Alternative: sweep the average BER for all models. Why: keeps each
  model's physical knob and shows the burst-length "cliff" of each code directly.
- `make bench` writes `results/bench.csv` (toll profile, exactly the required columns) and
  `results/bench_hospital.csv` (same sweep, hospital GE/burst parameters); `make plots` makes
  PNGs for both (suffix `_hospital`). PNGs are tracked as final plots, CSVs are ignored.
- The silent-wrong plot contrasts CRC-caught failures with silent errors on a log scale, because
  silent-wrong is 0 in all ~155k frames per code: CRC-32 makes it negligible (~2^-32 per
  failed frame). Zero counts are drawn as an upper bound (< 1/N).
- Plot colours: fixed colour per code from a CVD-validated categorical palette, so a code keeps
  its colour across all charts.

## Phase 6

- 🔀 Reed-Solomon uses the **whole 128-bit payload as one shortened codeword**: RS(16+2t, 16)
  over GF(256) (default t = 4 -> RS(24,16), rate 0.667), p(x) = x^8+x^4+x^3+x^2+1, roots
  alpha^1..alpha^2t. Alternative: several smaller RS codewords per frame with symbol
  interleaving. Why: simplest to explain, highest rate, and a single codeword already corrects
  every bit burst <= 8(t-1)+1 = 25 bits (tested exhaustively for every position).
- RS reuses the BCH Berlekamp-Massey routine (`bch_berlekamp_massey`, limit raised to t = 16) and
  adds Forney's formula for the error values. `--rs-t 1..16` travels in the packet header.
- 🔀 The interleaver is a generic **bit-level block interleaver** applied to the whole coded frame
  of ANY code (`--interleave D`, header field): rows written, columns sent. Alternative: symbol
  interleaver only for RS. Why: it is what rescues the bit-oriented codes (Hamming/BCH) in the
  hospital profile; RS does not need it (it would even spread a burst over more bytes).
  The "each block sees at most one burst error" guarantee holds when D divides the coded length.
- The benchmark adds interleaved variants `hamming74+il8` and `bch3116+il8` (drawn dashed in the
  base code's colour) and RS (`rs`, or `rs_t<N>` with `--rs-t N`).

## Week 2 - task 2: ARQ and Hybrid ARQ

- `--strategy {fec,arq,harq,adaptive}`, default `fec`: with fec no feedback is ever sent and all
  week-1 output is unchanged (verified by the existing integration tests).
- 🔀 `arq` sends **uncoded** frames (CRC-32 only) whatever `--code` says; `harq` is Hybrid ARQ
  type I (same FEC code on every retransmission, no soft combining). Alternative: incremental
  redundancy (type II). Why: type I is the textbook baseline and keeps every transmission
  independently decodable; IR would need receiver buffering across attempts.
- 🔀 **Simulated round trip**: the vehicle waits rtt/2 before each transmission and the tollgate
  waits rtt/2 before each ACK/NAK (`--rtt-ms`, default 20). Loopback itself is ~0 ms.
  Alternative: delay inside the channel process. Why: the feedback path bypasses the channel.
- Feedback (ACK/NAK + window statistics) goes tollgate -> vehicle directly on port 9002 and is
  never corrupted, only lost: `--fb-drop P` makes the tollgate drop feedback (failure injection).
  Stop-and-wait rules: feedback for another seq is ignored; an ACK for the current seq counts
  whatever attempt it answers; a NAK for an older attempt is ignored; no feedback within
  `--timeout-ms` (default 100) = NAK. At most `--max-retries` (default 4) retransmissions.
  The tollgate ACKs every copy of a seq it already holds a valid copy of (so a lost ACK costs
  one extra transmission, not a failure), ignores exact duplicates (same seq and attempt) and
  counts each seq once, with the class of its first accepted copy.
- Packet header version 2: two new bytes `attempt` and `strategy` at offsets 14-15 (old fields
  unchanged), feedback has its own 22-byte format (`include/feedback.h`).
- The stop-and-wait logic is one state machine (`src/sender.c`) used by both `bin/vehicle` and
  the in-process simulator `src/linksim.c`, so the benchmark measures the same protocol.
- 🔀 Latency definitions. Bench (simulated clock): **delivery latency** = time from the frame's
  first transmission until the receiver holds a CRC-valid copy, with airtime = coded bits /
  160 kbit/s (a typical RFID tag-to-reader rate), RTT 20 ms, timeout 100 ms. Undelivered
  frames count as failures and are excluded from latency. p99 = nearest-rank percentile. The UDP
  vehicle can only measure "until ACK minus the simulated return trip" (labelled as such).

## Week 2 - task 3: adaptive FEC controller

- 🔀 **Code ladder: L0 none -> L1 Hamming(7,4) -> L2 BCH(15,7) -> L3 BCH(31,16) -> L4 RS t=4 ->
  L5 RS t=8**, with NO interleaving at the top. Alternative (as specified): "rs plus interleaving
  at the top level". Why: measured on HOSPITAL_IMAGING (5000 frames): RS t=4 succeeds 83.4%,
  with bit interleaving d=2/4/8 only 77.4/69.5/59.8% - the bit interleaver spreads a burst over
  MORE bytes, the opposite of what a byte-symbol code wants. RS t=8 reaches 95.3% (toll 99.96%),
  dominating every other option. The ladder is one table in `src/adapt.c` if you want it back.
- 🔀 **Who decides**: the tollgate keeps the sliding window and reports raw window statistics in
  every ACK/NAK (frames, failures, corrected bits, coded bits); the VEHICLE runs the controller.
  Alternative: the tollgate decides and sends a level command. Why: the sender owns the code
  choice, and the receiver stays stateless about strategy (it decodes whatever the header says).
- 🔀 **Two signals**: step UP on the measured window failure rate f > up_threshold; step DOWN
  only when f <= down_threshold AND the binomial model predicts the next cheaper code would fail
  < down_threshold at the measured BER^ = corrected bits / coded bits (successful frames only).
  Alternative: thresholds on corrected bits per frame alone. Why: corrected-bit counts are not
  comparable across codes of different length and strength; the prediction makes "is the cheaper
  code good enough?" an explicit, explainable calculation (shown in the LEVEL DOWN message).
- 🔀 **Hysteresis**: (1) up 0.20 > down 0.05; (2) one decision per W fresh frames (disjoint
  windows; the window restarts at each level change); (3) probe back-off: a step down that has to
  be undone within 2W frames doubles the hold time before the next probe (max 16 windows), a
  probe that survives resets it. Defaults: --window 32, --up-threshold 0.20, --down-threshold
  0.05, --start-level 1. The first version evaluated the sliding window on every frame with
  up 0.15: it flapped L1 <-> L2 14 times in 3000 steady toll frames; now 0 (tested).
- HARQ is the fallback: a NAK is answered with a retransmission at the CURRENT level (which the
  same feedback may just have raised). BER^ ignores failed frames (unknown error count), so it
  under-estimates on bursty channels - the measured failure rate and the probe back-off cover that.
- 🔀 `--schedule toll:N,hospital:M,...` counts TRANSMISSIONS (air time), not frames; after the last
  segment the last profile stays. Each segment uses its profile's default model unless --model is
  given. The Gilbert-Elliott state is redrawn at a switch.
- Results (`make bench`, schedule toll:2000,hospital:2000,toll:2000, 5000 frames): adaptive
  99.94% correct, 0.076 retransmissions/frame, p99 33 ms, mean code rate 0.547; best fixed HARQ
  (BCH(31,16)) 99.40%, 0.198, p99 76 ms; ARQ 81.9%, 1.78, p99 94 ms; best plain FEC (RS) 91.4%.

## Week 2 - task 4: live dashboard

- 🔀 Dashboard uses the **Python standard library only** (`http.server` + one inline HTML/SVG page
  polling `/api/state` every 500 ms). Alternative: Flask (+ requirements.txt). Why: nothing to
  install on the presentation laptop; the page is small enough not to need a framework.
- Data path: the tollgate appends one JSON object per transmission to `--jsonl FILE` (flushed
  immediately); the server re-reads the file on each poll (runs are a few hundred lines). A frame's
  final class on the dashboard follows the same rule as the tollgate summary (first accepted copy).

## Final polish - fair comparison and goodput

- Strategy benchmark now includes the fixed baselines **HARQ + RS t=4** and **HARQ + RS t=8**
  (and FEC RS t=8), i.e. "always use the strongest code of the ladder".
- 🔀 **Goodput** = (CORRECT frames x 128 payload bits) / (all coded bits put on the air, parity and
  retransmissions included). Payload = ID + CRC (128 bits), consistent with the code-rate
  definition (128 / coded bits); counting only the 96 ID bits would scale every value by 0.75.
  The frame header is not counted (it is the same for every strategy).
- **Result (not tuned):** on toll:2000,hospital:2000,toll:2000 adaptive reaches 99.94% correct,
  goodput 0.505, p99 33 ms; HARQ + RS t=8 100%, goodput 0.490, p99 33 ms; HARQ + RS t=4 100%,
  goodput 0.612, p99 32 ms. The claim "adaptive matches the strongest code's reliability at
  clearly lower overhead" does NOT hold: the saving vs RS t=8 is ~3%, and HARQ + RS t=4 dominates.
  Cause: the ladder is ordered by burst robustness, not by code rate; its lower rungs (Hamming
  0.571, BCH 0.449/0.516) cost more parity than RS t=4 (0.667). A ladder of RS codes with
  t = 1..8 (rates 0.89 .. 0.50) would be the natural next experiment; it was deliberately not
  done here so the reported numbers are not the result of tuning after seeing the data.

## Final polish - RTT sensitivity (decision #18)

- **#18 addendum: the round-trip time is a modelling assumption** (default 20 ms; no real RFID
  link was measured). `make bench` now also runs the strategy comparison at RTT 5, 20 and 50 ms
  (`results/bench_rtt_<N>.csv`, plot `results/rtt_sensitivity.png`). On the scheduled channel the
  p99 latency grows linearly with RTT for every feedback strategy (ARQ 27 -> 94 -> 229 ms; HARQ +
  RS t=4 / t=8 and adaptive 11 -> 33 -> 78 ms), while plain FEC only pays one-way propagation
  (RS t=8: 4 -> 12 -> 27 ms). Rankings do not change with RTT; goodput does not depend on RTT.

## Overnight A - task 1: failure injection

What happens in each injected failure (all covered by `tests/test_packet.c` and
`tests/test_failure.sh`, which `make test` runs; `build/tests/inject` sends the bad packets):

| Case | What the program does | Exit / summary |
|---|---|---|
| Random garbage, any length 1-300 bytes | `ignoring malformed packet (N bytes)`, dropped | keeps running; `malformed=` in summary |
| **Empty (0-byte) datagram** | was read as "idle timeout" -> tollgate/channel STOPPED (bug, fixed: `net_recv` returns `NET_EMPTY`); now logged as malformed | keeps running |
| Truncated packet (every length tested), trailing bytes | length must equal 16 + B (+ 16 + B with sidecar), else malformed | keeps running |
| Wrong magic / version / type | malformed (all 256 values of each byte tested) | keeps running |
| Absurd nbits (65535, > 2048) | malformed, never read past the buffer | keeps running |
| Unknown code id / impossible rs_t, interleave 0, size mismatch, strategy > 3 | `dropping frame seq=N with unknown code/size/strategy` | `invalid=` in summary |
| ... same packet used to **steal the seq** (bug, fixed) | an invalid packet used to occupy its seq first, so the later VALID copy was thrown away as a duplicate and the bogus packet counted as a DETECTED_FAIL frame; validation now happens before any seq bookkeeping | |
| Duplicate seq (same attempt), late copy of an older seq | ignored as duplicate (decoded once) | `duplicates=` in summary |
| END lost | tollgate stops after `--idle-timeout-ms` (counted from the first packet) and prints the summary with `received N (END not seen)` | exit 0 |
| UDP port already in use | **bug, fixed**: `SO_REUSEADDR` let a 2nd tollgate/channel/vehicle bind the same UDP port on Linux and silently share/steal packets; removed (UDP has no TIME_WAIT). Now: `error: cannot bind UDP 127.0.0.1:P: Address already in use (is another instance running?)` | exit 1 |
| Burst longer than any code (L = 4096 > frame) for Hamming(7,4), BCH(31,16), RS t=8 | every frame DETECTED_FAIL (CRC catches it), 0 SILENT_WRONG, no crash | exit 0 |
| Feedback lost 100% (`--fb-drop 1`), harq and arq | vehicle times out, retransmits, prints `GIVING UP after max-retries+1 transmission(s)` for every frame | exit 0 |
| Garbage on the vehicle's feedback port | `ignoring malformed feedback (N bytes)`, keeps waiting for the real ACK/NAK | all frames still delivered |
| Header corruption (`--corrupt-header P`) | see decision #1 above: dropped as malformed/invalid, or misrouted by a flipped seq | never crashed (P up to 0.3 tested) |

Known limitations found (not fixed - design changes, see PROGRESS.md "Open problems"):
- A misrouted seq (corrupted header) makes the tollgate count a phantom frame, and with HARQ the
  real owner of the stolen seq can be rejected as a "late copy" on every retransmission until the
  vehicle gives up. The tollgate summary over-counts CORRECT in that case. Fix = header CRC.
- The tollgate's duplicate filter keeps only seq mod 65536 (already listed in PROGRESS.md).
- 🔀 Header corruption flips 1-4 random distinct bits (uniform) of the 16-byte header, END packets
  are exempt. Alternative: flip each header bit with the payload BER. Why: a per-packet event
  probability P is easy to explain and sweep, and END loss is tested separately (idle timeout).
- Test-script bug fixed on the way: `check "x" [ a ] && [ b ]` only checked `a` (the `&&` ran
  outside `check`); one such line in `tests/test_integration.sh` now uses `eval`.

## Overnight A - task 2: sanitizers and fuzzing

- `make sanitize` re-runs this Makefile with `BUILD=build/asan BINDIR=build/asan/bin` and
  `-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all` (CFLAGS passed through the
  environment so `-std=c11 ...` is kept), then runs ALL unit tests and both integration scripts
  against the sanitized binaries (scripts honour `BIN=`; default `bin`). ASan/LSan reports go to
  `build/asan/logs/`; UBSan prints to stderr even with `log_path`, so the target also greps the
  outputs captured in `results/tmp/` (planted-error check: both paths are detected).
- `tests/test_fuzz.c` (seeded, ~0.8 s, part of `make test`): 200k buffers for `packet_parse`
  (100k random, 100k mutated valid packets of all 7 codes; every buffer that parses must
  re-serialize to the same length and parse to the same packet, and parsed DATA packets go through
  the real receive path `rx_decode_packet` + `rx_classify` - ~12.5k reach the decoders), 200k for
  `feedback_parse` (exact byte round trip), 50k random `--schedule` strings.
- **Findings:**
  1. **Reed-Solomon decoder read uninitialized memory (fixed).** `rs_decode` returned -1 on an
     uncorrectable word without writing `data`; `fec.c` then unpacked uninitialized stack bytes
     as the decoded payload. Classification was never affected (a flagged block is always
     DETECTED_FAIL), but the logged `tag_rx`, `crc_ok` and the dashboard's ID of failed RS frames
     were garbage and not reproducible. Found because `tests/test_receiver.c` (decode twice, compare)
     failed only in the sanitized build. Fix: output the received data bytes first (as
     `bch_decode` already did); test `failure_outputs_received_data_bytes` in `tests/test_rs.c`.
  2. No ASan/UBSan/LSan finding in any other unit test, the fuzz test, the integration test or the
     failure-injection test (gcc 13). The fuzzer found no parser bug.
- `make sanitize CC=clang` could not be run here: this container lacks clang's ASan runtime
  (`libclang_rt.asan-x86_64.a`, package `libclang-rt-18-dev`). Not a project problem.

## Overnight A - task 3: multi-seed statistics

- 🔀 `bin/bench --strategies --seeds N` runs the strategy comparison for seeds seed..seed+N-1
  (`make bench`: N = 20, seeds 42..61) and reports mean +- **95% confidence interval from
  Student's t distribution** (N-1 = 19 degrees of freedom, t = 2.093; `src/stats.c`), not a
  bootstrap. Alternative: bootstrap percentile CI. Why: t is explainable in two lines and each
  per-seed value is already an average over 5000 frames (close to normal). Caveat: the p99 latency
  of a HARQ code is quantized in steps of one round of retransmission, so its per-seed values can
  be bimodal (HARQ + RS t=4: 32.4 or 53.6 ms) - the t-interval of that mean is approximate.
- Comparisons between strategies use **paired differences** over the same seeds
  (`results/bench_strategy_seeds_diff.csv`): every strategy sees the same tags and the same noise
  per seed, so the per-seed difference removes the shared channel luck. "Significant" = the 95% CI
  of the mean difference excludes 0. Default behaviour without `--seeds` is unchanged (checked:
  `bench_strategy.csv` identical except the timing-noise latency columns).
- **Results, scheduled channel toll -> hospital -> toll, 20 seeds** (adaptive minus baseline):

  | metric | adaptive - HARQ+RS t=8 | adaptive - HARQ+RS t=4 |
  |---|---|---|
  | success rate | -0.051 +- 0.009 pp | -0.049 +- 0.009 pp |
  | goodput | **+0.0133 +- 0.0024** | -0.1083 +- 0.0030 |
  | retx / frame | +0.051 +- 0.002 | **-0.019 +- 0.003** |
  | p99 latency | +0.002 ms (decode-time noise, practically 0) | **-8.7 +- 5.1 ms** |

  So: (1) adaptive's goodput advantage over HARQ + RS t=8 IS outside the confidence interval, but
  it is small (+2.7%) and costs 0.05 pp of success. (2) HARQ + RS t=4 is NOT better on every
  metric: it wins success and goodput clearly, adaptive wins retransmissions/frame and p99
  latency. The single-seed "RS t=4 beats adaptive on every metric" (final polish) was wrong:
  retransmissions were already lower for adaptive in that run (0.076 vs 0.089), and RS t=4's
  p99 of 32 ms was seed luck (mean 42 +- 5 ms). README table now shows the 20-seed means.

## Overnight A - task 4: all-RS ladder experiment (NEW experiment, pre-registered)

Written and committed BEFORE any code for it was written or any run was made.

- **Hypothesis:** "An adaptive ladder made only of RS codes t=1,2,4,6,8 (code rates 0.89 .. 0.50)
  reaches HARQ+RS t=8 reliability at clearly higher goodput than the current ladder."
- **Success criterion** (both must hold on the scheduled channel):
  1. Goodput higher than both the current adaptive ladder (`mixed`) and HARQ + RS t=8 by more than
     the 95% CI: the 95% CI of the PAIRED difference (all-RS adaptive minus X, same 20 seeds) lies
     entirely above 0, for X = mixed adaptive and X = HARQ + RS t=8. (Also reported: whether the
     unpaired per-strategy CIs overlap.)
  2. Success rate within 0.5 percentage points of HARQ + RS t=8:
     mean success(all-RS adaptive) >= mean success(HARQ + RS t=8) - 0.005.
- **Fixed setup (no tuning after seeing results):** ladder `rs` = L0 RS(18,16) t=1 (rate 0.889),
  L1 RS(20,16) t=2 (0.800), L2 RS(24,16) t=4 (0.667), L3 RS(28,16) t=6 (0.571), L4 RS(32,16) t=8
  (0.500). Same controller code and defaults as today: window W = 32, up 0.20, down 0.05,
  `--start-level 1` (the same index as today; = RS t=2 on this ladder), probe back-off max 16
  windows, the same binomial step-down predictor (its RS byte-error model already exists). Same
  link simulator, RTT 20 ms, timeout 100 ms, max 4 retries, schedule toll:2000,hospital:2000,
  toll:2000, 5000 frames, seeds 42..61 (`--seeds 20 --seed 42`). Selected with `--ladder rs`;
  default `--ladder mixed` must leave every existing result unchanged. The toll-only and
  hospital-only channels are reported too, but the criterion is judged on the scheduled channel.
