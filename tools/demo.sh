#!/bin/sh
# One-terminal demo: starts tollgate, then channel, then vehicle with the SAME flags,
# waits for them to finish and leaves logs in results/tmp/.
#
#   sh tools/demo.sh --profile toll --code hamming74 --count 20 --delay-ms 250 [more flags]
#
# Output lines are prefixed [VEHICLE] / [CHANNEL] / [TOLLGATE] by the programs themselves.
#
# Environment: BIN (binary directory, default bin; prepended with "./", so it should be
# relative to the repo root), SHOWBITS (0 = omit --show-bits for the tollgate).
# Every argument is passed unchanged to all three programs. The script appends its own
# --show-bits, --log and --summary-file to the tollgate and --log to the channel AFTER "$@",
# so a --log or --summary-file given by the caller is overridden (the last occurrence wins).
# Files written: results/tmp/{tollgate.csv,summary.txt,channel.csv}.
# Exit status: that of the vehicle if it failed, else that of the tollgate, 1 if a binary is
# missing, 130 if interrupted.
# The tollgate and channel stop only on their idle timeout (--idle-timeout-ms, default 10 s,
# counted from the first packet), so the script returns that long after the vehicle ends. With
# --count 0 or --idle-timeout-ms 0 no timeout applies and the script waits until interrupted.
cd "$(dirname "$0")/.." || exit 1
BIN=${BIN:-bin}      # make sanitize sets BIN=build/asan/bin
SHOWBITS=${SHOWBITS:-1}   # 0 = no per-frame bit maps (make demo-arq / demo-harq default)
# $SB is deliberately expanded unquoted below: empty means "no argument", not "".
SB="--show-bits"; [ "$SHOWBITS" = 0 ] && SB=""
mkdir -p results/tmp

for b in $BIN/tollgate $BIN/channel $BIN/vehicle; do
    [ -x "$b" ] || { echo "missing $b - run 'make' first" >&2; exit 1; }
done

# Receiver-first start order matters: the programs use UDP, so a datagram sent before its
# receiver has bound its port is lost. Flow is vehicle -> channel -> tollgate, hence tollgate
# first. The 0.3 s sleeps are a fixed grace period, not a readiness check; a bind failure
# (e.g. port already in use) is not detected here and shows up only as lost or missing frames.
TG=""; CH=""
# Stops the background receivers; does not wait for them.
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
# A failed vehicle (bad flags, socket error) must not leave the receivers waiting for their
# idle timeout, so they are killed and the vehicle's status is returned.
if [ "$VRC" -ne 0 ]; then
    echo "vehicle failed (exit $VRC) - stopping the other processes" >&2
    cleanup
    exit "$VRC"
fi
wait "$CH"      # status ignored; only the tollgate's status is reported
wait "$TG"
TRC=$?
echo "=== demo finished - per-frame logs: results/tmp/tollgate.csv, results/tmp/channel.csv ==="
exit "$TRC"
