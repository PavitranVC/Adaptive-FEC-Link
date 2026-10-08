#!/bin/sh
# Integration test: runs the real three-process UDP pipeline (on non-default ports so it does
# not clash with a demo running on 9000/9001) and checks the tollgate summary.
cd "$(dirname "$0")/.." || exit 1
mkdir -p results/tmp
PORTS="--channel-port 19100 --tollgate-port 19101 --feedback-port 19102"
OUT=results/tmp/integration.out
fail=0

check() { # check <description> <command...>
    desc=$1; shift
    if "$@"; then echo "  $desc ok"; else echo "  $desc FAILED"; fail=1; fi
}
val() { grep "^$1=" results/tmp/summary.txt | cut -d= -f2; }

echo "== integration: toll profile, hamming74, 40 frames"
sh tools/demo.sh $PORTS --no-color --profile toll --code hamming74 --seed 5 --count 40 \
    --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
rc=$?
check "demo exit code 0" [ "$rc" -eq 0 ]
check "vehicle output" grep -q '^\[VEHICLE\] seq=' "$OUT"
check "channel output" grep -q '^\[CHANNEL\] seq=' "$OUT"
check "tollgate output" grep -q '^\[TOLLGATE\] seq=' "$OUT"
check "summary printed" grep -q 'SUMMARY' "$OUT"
check "sent 40" [ "$(val sent)" = 40 ]
check "received 40" [ "$(val received)" = 40 ]
check "lost 0" [ "$(val lost)" = 0 ]
total=$(( $(val correct) + $(val detected_fail) + $(val silent_wrong) ))
check "classes add up to 40" [ "$total" -eq 40 ]
check "mostly correct (>= 30)" [ "$(val correct)" -ge 30 ]
cut -d, -f1-6,8,9 results/tmp/tollgate.csv >results/tmp/run1.csv

echo "== integration: same seed again is bit-identical (ignoring latency)"
sh tools/demo.sh $PORTS --no-color --profile toll --code hamming74 --seed 5 --count 40 \
    --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
cut -d, -f1-6,8,9 results/tmp/tollgate.csv >results/tmp/run2.csv
check "reproducible per-frame log" cmp -s results/tmp/run1.csv results/tmp/run2.csv

echo "== integration: hospital profile (bursty), secded84"
sh tools/demo.sh $PORTS --no-color --profile hospital --code secded84 --seed 9 --count 30 \
    --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "hospital run ok" [ $? -eq 0 ]
check "hospital profile reported" [ "$(val profile)" = HOSPITAL_IMAGING ]
check "ge model is hospital default" [ "$(val model)" = ge ]
check "received 30" [ "$(val received)" = 30 ]

echo "== integration: bch3116 end to end"
sh tools/demo.sh $PORTS --no-color --profile toll --code bch3116 --seed 4 --count 30 \
    --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "bch3116 run ok" [ $? -eq 0 ]
check "bch3116 received 30" [ "$(val received)" = 30 ]
check "bch3116 code reported" [ "$(val code)" = bch3116 ]

echo "== integration: Reed-Solomon t=6 + interleaver header fields travel end to end"
sh tools/demo.sh $PORTS --no-color --profile hospital --code rs --rs-t 6 --seed 4 --count 30 \
    --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "rs run ok" [ $? -eq 0 ]
check "rs received 30" [ "$(val received)" = 30 ]
check "rs t=6 -> 28 bytes = 224 coded bits" [ "$(val coded_bits)" = 224 ]
sh tools/demo.sh $PORTS --no-color --profile toll --code hamming74 --interleave 8 --model burst \
    --burst-len 8 --p 1 --seed 4 --count 30 --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "interleaved hamming74 fixes every 8-bit burst" [ "$(val correct)" = 30 ]

echo "== integration: compare script (default codes hamming74 vs bch157)"
sh tools/compare.sh $PORTS --seed 3 --count 50 >"$OUT" 2>&1
check "compare ok" [ $? -eq 0 ]
check "compare shows both codes" grep -q 'hamming74.*bch157' "$OUT"
check "compare shows silent_wrong row" grep -q '^silent_wrong' "$OUT"

echo "== integration: Stop-and-Wait ARQ over the feedback port"
sh tools/demo.sh $PORTS --no-color --profile toll --strategy arq --seed 8 --count 20 \
    --rtt-ms 2 --timeout-ms 50 --max-retries 10 --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "arq run ok" [ $? -eq 0 ]
check "arq strategy reported" [ "$(val strategy)" = arq ]
check "arq every frame accounted once" [ "$(val received)" = 20 ]
check "arq retransmitted at least once" [ "$(val retransmissions)" -ge 1 ]
check "arq sent uncoded frames" [ "$(val code)" = none ]
check "arq vehicle saw ACKs" grep -q 'ACK after' "$OUT"

echo "== integration: Hybrid ARQ with 30% injected feedback loss"
sh tools/demo.sh $PORTS --no-color --profile toll --strategy harq --code hamming74 --seed 8 \
    --count 20 --rtt-ms 2 --timeout-ms 40 --fb-drop 0.3 --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "harq run ok" [ $? -eq 0 ]
check "harq all 20 frames correct" [ "$(val correct)" = 20 ]
check "harq feedback really dropped" [ "$(val feedback_dropped)" -ge 1 ]
check "harq vehicle timed out" grep -q 'timeout (no feedback' "$OUT"

echo "== integration: adaptive controller on a toll -> hospital -> toll schedule"
sh tools/demo.sh $PORTS --no-color --strategy adaptive --schedule toll:40,hospital:100,toll:200 \
    --window 8 --seed 42 --count 200 --rtt-ms 1 --delay-ms 0 --idle-timeout-ms 3000 >"$OUT" 2>&1
check "adaptive run ok" [ $? -eq 0 ]
check "adaptive strategy reported" [ "$(val strategy)" = adaptive ]
check "adaptive every frame accounted once" [ "$(val received)" = 200 ]
check "channel switched profile" grep -q 'interference changes: HOSPITAL_IMAGING' "$OUT"
check "controller stepped up" grep -q 'LEVEL UP' "$OUT"
check "controller stepped back down" grep -q 'LEVEL DOWN' "$OUT"
check "adaptive mostly correct (>= 190)" [ "$(val correct)" -ge 190 ]

echo "== integration: live JSON-lines log + dashboard server"
sh tools/demo.sh $PORTS --no-color --quiet --strategy harq --code hamming74 --seed 2 --count 25 \
    --rtt-ms 1 --delay-ms 0 --idle-timeout-ms 3000 --jsonl results/tmp/live_test.jsonl >"$OUT" 2>&1
check "jsonl run ok" [ $? -eq 0 ]
check "one json line per transmission" [ "$(wc -l <results/tmp/live_test.jsonl | tr -d ' ')" = "$(val transmissions)" ]
if command -v python3 >/dev/null 2>&1; then
    check "dashboard aggregates the log" python3 -c "
import json, subprocess, sys
s = json.loads(subprocess.check_output([sys.executable, 'tools/dashboard.py', '--log', 'results/tmp/live_test.jsonl', '--once']))
sys.exit(0 if s['frames'] == 25 and sum(s['counts'].values()) == 25 else 1)"
    check "dashboard serves page and state" python3 -c "
import json, subprocess, sys, time, urllib.request
p = subprocess.Popen([sys.executable, 'tools/dashboard.py', '--log', 'results/tmp/live_test.jsonl', '--port', '19180'], stdout=subprocess.DEVNULL)
try:
    for _ in range(50):
        try:
            page = urllib.request.urlopen('http://127.0.0.1:19180/').read().decode(); break
        except OSError: time.sleep(0.1)
    state = json.loads(urllib.request.urlopen('http://127.0.0.1:19180/api/state').read())
    sys.exit(0 if 'FEC link' in page and state['frames'] == 25 else 1)
finally:
    p.terminate()"
fi

echo "== integration: bench smoke test"
./bin/bench --profile toll --frames 50 --code hamming74 --out results/tmp/bench_smoke.csv >"$OUT" 2>&1
check "bench ok" [ $? -eq 0 ]
check "bench csv header" grep -q '^code,model,param,frames,frame_success_rate,detected_fail_rate,silent_wrong_rate,code_rate,mean_decode_us$' results/tmp/bench_smoke.csv
rows=$(grep -c '^hamming74,' results/tmp/bench_smoke.csv)
check "bench csv has rows for all 3 models" [ "$rows" -ge 3 ] && grep -q ',burst,' results/tmp/bench_smoke.csv
./bin/bench --strategies --schedule toll:30,hospital:30 --trace results/tmp/trace_smoke.csv \
    --frames 50 --out results/tmp/bench_strategy_smoke.csv >"$OUT" 2>&1
check "bench --strategies ok" [ $? -eq 0 ]
check "strategy csv header" grep -q '^strategy,code,channel,frames,success_rate,silent_wrong_rate,retx_per_frame,mean_latency_ms,p99_latency_ms,mean_code_rate,level_changes,goodput$' results/tmp/bench_strategy_smoke.csv
check "strategy csv has arq and harq rows" grep -q '^arq,' results/tmp/bench_strategy_smoke.csv
check "strategy csv has adaptive on the schedule" grep -q '^adaptive,ladder,schedule,' results/tmp/bench_strategy_smoke.csv
check "adaptive trace written" grep -q '^frame,tx,segment,level,attempts,class,latency_ms$' results/tmp/trace_smoke.csv
if command -v python3 >/dev/null 2>&1 && python3 -c "import matplotlib" 2>/dev/null; then
    python3 tools/plot.py results/tmp/bench_strategy_smoke.csv --outdir results/tmp >"$OUT" 2>&1
    check "strategy plot runs" [ $? -eq 0 ]
    python3 tools/plot.py results/tmp/trace_smoke.csv --outdir results/tmp >"$OUT" 2>&1
    check "adaptive level plot runs" [ $? -eq 0 ]
    python3 tools/plot.py results/tmp/bench_smoke.csv --outdir results/tmp >"$OUT" 2>&1
    check "plot.py runs" [ $? -eq 0 ]
else
    echo "  plot.py skipped (matplotlib not installed)"
fi

if [ "$fail" -ne 0 ]; then
    echo "Integration test FAILED - last output:"
    cat "$OUT"
    exit 1
fi
echo "Integration test passed."
