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

echo "== integration: bench smoke test"
./bin/bench --profile toll --frames 50 --code hamming74 --out results/tmp/bench_smoke.csv >"$OUT" 2>&1
check "bench ok" [ $? -eq 0 ]
check "bench csv header" grep -q '^code,model,param,frames,frame_success_rate,detected_fail_rate,silent_wrong_rate,code_rate,mean_decode_us$' results/tmp/bench_smoke.csv
rows=$(grep -c '^hamming74,' results/tmp/bench_smoke.csv)
check "bench csv has rows for all 3 models" [ "$rows" -ge 3 ] && grep -q ',burst,' results/tmp/bench_smoke.csv
if command -v python3 >/dev/null 2>&1 && python3 -c "import matplotlib" 2>/dev/null; then
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
