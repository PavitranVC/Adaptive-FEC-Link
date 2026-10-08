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

echo "== integration: compare script"
CODES="hamming74 secded84" sh tools/compare.sh $PORTS --seed 3 --count 50 >"$OUT" 2>&1
check "compare ok" [ $? -eq 0 ]
check "compare shows both codes" grep -q 'hamming74.*secded84' "$OUT"
check "compare shows silent_wrong row" grep -q '^silent_wrong' "$OUT"

if [ "$fail" -ne 0 ]; then
    echo "Integration test FAILED - last output:"
    cat "$OUT"
    exit 1
fi
echo "Integration test passed."
