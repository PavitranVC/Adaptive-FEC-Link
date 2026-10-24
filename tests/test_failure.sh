#!/bin/sh
# Failure-injection integration test (docs/ROADMAP.md, v0.2): every receiver must LOG and DROP bad
# input and never crash; a broken setup must fail loudly with a non-zero exit code.
# Uses ports 19200-19202 so it never clashes with a demo or tests/test_integration.sh.
cd "$(dirname "$0")/.." || exit 1
BIN=${BIN:-bin}      # make sanitize sets BIN=build/asan/bin
BUILD=${BUILD:-build}
mkdir -p results/tmp
T=results/tmp/failure
CP=19200; TP=19201; FP=19202
PORTS="--channel-port $CP --tollgate-port $TP --feedback-port $FP"
INJ=./$BUILD/tests/inject
fail=0

check() { # check <description> <command...>
    desc=$1; shift
    if "$@"; then echo "  $desc ok"; else echo "  $desc FAILED"; fail=1; fi
}
val() { v=$(grep "^$2=" "$1" 2>/dev/null | cut -d= -f2); echo "${v:-0}"; }   # val <file> <key>
count() { grep -c "$1" "$2" 2>/dev/null || true; }                            # count <regex> <file>
# brief <outfile> <cmd...>: run cmd for at most 2 s (a regression must not hang make test);
# sets rc to its exit code, or 124 if it had to be killed.
brief() {
    out=$1; shift
    "$@" >"$out" 2>&1 &
    bp=$!
    i=0
    while kill -0 $bp 2>/dev/null && [ $i -lt 20 ]; do sleep 0.1; i=$((i + 1)); done
    if kill -0 $bp 2>/dev/null; then kill $bp 2>/dev/null; wait $bp 2>/dev/null; rc=124
    else wait $bp; rc=$?; fi
}

echo "== failure: malformed packets straight into the tollgate, END lost"
rm -f $T.sum
./$BIN/tollgate $PORTS --no-color --idle-timeout-ms 1500 --summary-file $T.sum >$T.tg 2>&1 &
TG=$!
sleep 0.3
$INJ --port $TP malformed >$T.inj
wait $TG
rc=$?
check "tollgate survives and exits 0" [ "$rc" -eq 0 ]
check "stops on idle timeout (END lost)" grep -q 'idle timeout - stopping' $T.tg
check "summary printed without END" grep -q 'END not seen' $T.tg
garbage=$(awk '$1=="garbage"{print $2}' $T.inj); malformed=$(awk '$1=="malformed"{print $2}' $T.inj)
check "every garbage/truncated/bad-magic/version/type/nbits packet logged" \
    [ "$(count 'ignoring malformed packet' $T.tg)" -eq $((garbage + malformed)) ]
check "malformed counted in summary" [ "$(val $T.sum malformed)" -eq $((garbage + malformed)) ]
check "unknown code id rejected by packet_parse (counted as malformed above, not invalid)" [ "$(val $T.sum invalid)" -eq 0 ]
check "the 2 valid frames are received" [ "$(val $T.sum received)" -eq 2 ]
check "... and CORRECT (invalid packet did not steal seq 0)" [ "$(val $T.sum correct)" -eq 2 ]
check "duplicate seq + late copy ignored" [ "$(val $T.sum duplicates)" -eq 2 ]

echo "== failure: malformed packets into the channel, then END"
rm -f $T.sum
./$BIN/tollgate $PORTS --no-color --idle-timeout-ms 3000 --summary-file $T.sum >$T.tg 2>&1 &
TG=$!
sleep 0.3
./$BIN/channel $PORTS --no-color --idle-timeout-ms 3000 >$T.ch 2>&1 &
CH=$!
sleep 0.3
$INJ --port $CP malformed >$T.inj
$INJ --port $CP end 2
wait $CH; crc=$?
wait $TG; trc=$?
check "channel and tollgate exit 0" eval '[ "$crc" -eq 0 ] && [ "$trc" -eq 0 ]'
check "channel logged the malformed packets" [ "$(count 'ignoring malformed packet' $T.ch)" -eq $((garbage + malformed)) ]
check "tollgate got only valid frames" eval '[ "$(val $T.sum malformed)" -eq 0 ] && [ "$(val $T.sum received)" -eq 2 ]'
check "END forwarded: sent 2, lost 0" eval '[ "$(val $T.sum sent)" -eq 2 ] && [ "$(val $T.sum lost)" -eq 0 ]'

echo "== failure: UDP port already in use"
./$BIN/tollgate $PORTS --no-color --idle-timeout-ms 0 >$T.tg1 2>&1 &
TG=$!
./$BIN/channel $PORTS --no-color --idle-timeout-ms 0 >$T.ch1 2>&1 &
CH=$!
sleep 0.3
brief $T.tg2 ./$BIN/tollgate $PORTS --no-color
check "second tollgate exits non-zero at once" eval '[ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]'
check "second tollgate says why" grep -q "cannot bind UDP 127.0.0.1:$TP" $T.tg2
brief $T.ch2 ./$BIN/channel $PORTS --no-color
check "second channel exits non-zero at once" eval '[ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]'
check "second channel says why" grep -q "cannot bind UDP 127.0.0.1:$CP" $T.ch2
kill $TG $CH 2>/dev/null; wait $TG $CH 2>/dev/null
./$BIN/vehicle $PORTS --no-color --strategy harq --count 1 >$T.v1 2>&1 &
V1=$!
sleep 0.3
brief $T.v2 ./$BIN/vehicle $PORTS --no-color --strategy harq --count 1
check "second vehicle (feedback port taken) exits non-zero at once" eval '[ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]'
check "second vehicle says why" grep -q "cannot bind UDP 127.0.0.1:$FP" $T.v2
kill $V1 2>/dev/null; wait $V1 2>/dev/null

echo "== failure: bursts longer than any code can correct"
for code in hamming74 bch3116 rs; do
    sh tools/demo.sh $PORTS --no-color --quiet --code $code --rs-t 8 --model burst --burst-len 4096 \
        --p 1 --seed 3 --count 20 --delay-ms 0 --idle-timeout-ms 3000 >$T.out 2>&1
    rc=$?
    cp results/tmp/summary.txt $T.sum
    check "$code: run ok, all 20 received" eval '[ "$rc" -eq 0 ] && [ "$(val $T.sum received)" -eq 20 ]'
    check "$code: every frame DETECTED_FAIL, none silent" \
        eval '[ "$(val $T.sum detected_fail)" -eq 20 ] && [ "$(val $T.sum silent_wrong)" -eq 0 ]'
done

echo "== failure: feedback lost 100% (vehicle gives up after --max-retries)"
for strat in harq arq; do
    sh tools/demo.sh $PORTS --no-color --strategy $strat --fb-drop 1 --max-retries 2 --timeout-ms 20 \
        --rtt-ms 1 --seed 3 --count 5 --delay-ms 0 --idle-timeout-ms 3000 >$T.out 2>&1
    rc=$?
    cp results/tmp/summary.txt $T.sum
    check "$strat: run ok" [ "$rc" -eq 0 ]
    check "$strat: vehicle gave up on all 5 frames" [ "$(count 'GIVING UP after 3 transmission' $T.out)" -eq 5 ]
    check "$strat: 3 transmissions per frame reached the tollgate" [ "$(val $T.sum transmissions)" -eq 15 ]
    check "$strat: all 15 feedbacks dropped" [ "$(val $T.sum feedback_dropped)" -eq 15 ]
done

echo "== failure: garbage on the vehicle's feedback port"
sh tools/demo.sh $PORTS --no-color --strategy harq --rtt-ms 2 --seed 3 --count 40 --delay-ms 15 \
    --idle-timeout-ms 3000 >$T.out 2>&1 &
D=$!
sleep 0.8
$INJ --port $FP fbgarbage >/dev/null
wait $D; rc=$?
cp results/tmp/summary.txt $T.sum
check "harq run with feedback garbage ok" [ "$rc" -eq 0 ]
check "vehicle logged malformed feedback" grep -q 'ignoring malformed feedback' $T.out
check "all 40 frames delivered" eval '[ "$(val $T.sum received)" -eq 40 ] && [ "$(val $T.sum correct)" -eq 40 ]'

echo "== failure: header corruption (--corrupt-header)"
sh tools/demo.sh $PORTS --no-color --quiet --code rs --seed 6 --count 200 --delay-ms 0 \
    --idle-timeout-ms 3000 >$T.out 2>&1
cut -d, -f1-6,8,9 results/tmp/tollgate.csv >$T.base
sh tools/demo.sh $PORTS --no-color --quiet --code rs --seed 6 --count 200 --delay-ms 0 \
    --idle-timeout-ms 3000 --corrupt-header 0 >$T.out 2>&1
cut -d, -f1-6,8,9 results/tmp/tollgate.csv >$T.zero
check "--corrupt-header 0 changes nothing" cmp -s $T.base $T.zero
sh tools/demo.sh $PORTS --no-color --quiet --code rs --seed 6 --count 200 --delay-ms 0 \
    --idle-timeout-ms 3000 --corrupt-header 0.3 >$T.out 2>&1
rc=$?
cp results/tmp/summary.txt $T.sum
check "fec run with 30% header corruption ok" [ "$rc" -eq 0 ]
check "channel reports corrupted headers" grep -q 'header corrupted in [1-9]' $T.out
check "tollgate dropped some packets" [ $(( $(val $T.sum malformed) + $(val $T.sum invalid) )) -ge 1 ]
check "summary still printed" grep -q 'SUMMARY' $T.out
sh tools/demo.sh $PORTS --no-color --quiet --strategy harq --code rs --rtt-ms 1 --timeout-ms 20 \
    --seed 6 --count 100 --delay-ms 0 --idle-timeout-ms 3000 --corrupt-header 0.2 >$T.out 2>&1
check "harq run with 20% header corruption ok" [ $? -eq 0 ]

if [ "$fail" -ne 0 ]; then
    echo "Failure-injection test FAILED"
    exit 1
fi
echo "Failure-injection test passed."
