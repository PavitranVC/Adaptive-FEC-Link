#!/bin/sh
# One-terminal demo: starts tollgate, then channel, then vehicle with the SAME flags,
# waits for them to finish and leaves logs in results/tmp/.
#
#   sh tools/demo.sh --profile toll --code hamming74 --count 20 --delay-ms 250 [more flags]
#
# Output lines are prefixed [VEHICLE] / [CHANNEL] / [TOLLGATE] by the programs themselves.
cd "$(dirname "$0")/.." || exit 1
BIN=${BIN:-bin}      # make sanitize sets BIN=build/asan/bin
SHOWBITS=${SHOWBITS:-1}   # 0 = no per-frame bit maps (make demo-arq / demo-harq default)
SB="--show-bits"; [ "$SHOWBITS" = 0 ] && SB=""
mkdir -p results/tmp

for b in $BIN/tollgate $BIN/channel $BIN/vehicle; do
    [ -x "$b" ] || { echo "missing $b - run 'make' first" >&2; exit 1; }
done

TG=""; CH=""
cleanup() {
    [ -n "$CH" ] && kill "$CH" 2>/dev/null
    [ -n "$TG" ] && kill "$TG" 2>/dev/null
}
trap 'cleanup; exit 130' INT TERM

echo "=== Adaptive FEC link demo: tollgate <- channel <- vehicle ($*) ==="
./$BIN/tollgate "$@" $SB --log results/tmp/tollgate.csv \
    --summary-file results/tmp/summary.txt &
TG=$!
sleep 0.3
./$BIN/channel "$@" --log results/tmp/channel.csv &
CH=$!
sleep 0.3
./$BIN/vehicle "$@"
VRC=$?
if [ "$VRC" -ne 0 ]; then
    echo "vehicle failed (exit $VRC) - stopping the other processes" >&2
    cleanup
    exit "$VRC"
fi
wait "$CH"
wait "$TG"
TRC=$?
echo "=== demo finished - per-frame logs: results/tmp/tollgate.csv, results/tmp/channel.csv ==="
exit "$TRC"
