#!/bin/sh
# Runs the full UDP pipeline once per code with the SAME seed and flags and prints the
# tollgate summaries side by side.
#
#   CODES="hamming74 bch157" sh tools/compare.sh --seed 42 --count 500 --profile toll
#
# Default CODES: "hamming74 bch157". Extra flags are passed to all three programs.
#
# Environment: CODES (space-separated code names, each used verbatim in file names and as
# --code), BIN (binary directory, default bin, relative to the repo root).
# Per code: starts tollgate, channel, vehicle (in that order; see demo.sh for why) and waits
# for the receivers to hit their idle timeout (default 10 s after the last packet), so each
# code costs at least that long. A code given in "$@" is overridden by the --code appended here.
# Files: results/tmp/compare_<code>.{txt,log}; .txt is the tollgate key=value summary.
# Output: one table, a row per summary key and a column per code. Rows follow the key order of
# the FIRST summary file; a key missing from a later file prints as empty.
# Exit status: 0, 2 for a bad code name or non-positive --count/--idle-timeout-ms, or 1 if a UDP
# port is in use, a vehicle run fails or a summary file was not produced.
cd "$(dirname "$0")/.." || exit 1
BIN=${BIN:-bin}      # make sanitize sets BIN=build/asan/bin
# --count 0 / --idle-timeout-ms 0 (or any non-positive or non-numeric value) would leave the
# receivers waiting forever, so reject them before anything is started.
prev=""
for a in "$@"; do
    case $prev in
    --count|--idle-timeout-ms)
        case $a in
        ''|*[!0-9]*) echo "$prev must be a positive integer (got '$a')" >&2; exit 2 ;;
        esac
        [ "$a" -gt 0 ] || { echo "$prev must be a positive integer (got '$a')" >&2; exit 2; }
        ;;
    esac
    prev=$a
done

mkdir -p results/tmp
CODES=${CODES:-"hamming74 bch157"}
# Code names end up in file names under results/tmp/, so only [A-Za-z0-9_] is accepted
# (no spaces, globs or '/'). Checked before anything is started;
# set -f stops $CODES from globbing.
set -f
for code in $CODES; do
    case $code in
    ''|*[!A-Za-z0-9_]*) echo "invalid code name '$code' (allowed: A-Za-z0-9_)" >&2; exit 2 ;;
    esac
done

# A UDP port that is already bound (an old run still alive) goes unnoticed otherwise and shows
# up only as lost frames. Checks the three ports (flag values from "$@", else 9000-9002) with
# ss, else /proc/net/udp; if neither is available the check is skipped.
port_busy() {
    if command -v ss >/dev/null 2>&1; then
        ss -uln 2>/dev/null | awk -v p=":$1" '{ n = length($4) - length(p) + 1; if (n > 0 && substr($4, n) == p) f = 1 } END { exit !f }'
    elif [ -r /proc/net/udp ]; then      # udp6 may be absent: list only readable files
        h=$(printf '%04X' "$1")
        awk -v h=":$h" '{ n = length($2) - 4; if (n > 0 && substr($2, n) == h) f = 1 } END { exit !f }' /proc/net/udp $(ls /proc/net/udp6 2>/dev/null)
    else
        return 1
    fi
}
cp=9000; tp=9001; fp=9002; prev=""
for a in "$@"; do
    case $prev in
    --channel-port) cp=$a ;;
    --tollgate-port) tp=$a ;;
    --feedback-port) fp=$a ;;
    esac
    prev=$a
done
for p in $cp $tp $fp; do
    case $p in ''|*[!0-9]*) continue ;; esac     # not a number: the programs report it
    if port_busy "$p"; then
        echo "UDP port $p is already in use - an old run may still be alive." >&2
        echo "List it: pgrep -fl \"bin/(vehicle|channel|tollgate)\"; stop it by PID: kill <pid>" >&2
        echo "(or use other ports: --channel-port/--tollgate-port/--feedback-port)" >&2
        exit 1
    fi
done

files=""
for code in $CODES; do
    sum="results/tmp/compare_$code.txt"
    log="results/tmp/compare_$code.log"
    rm -f "$sum"      # so a stale summary from an earlier run cannot be mistaken for this one
    ./$BIN/tollgate "$@" --code "$code" --quiet --no-color --summary-file "$sum" >"$log" 2>&1 &
    tg=$!
    sleep 0.3
    ./$BIN/channel "$@" --code "$code" --quiet --no-color >>"$log" 2>&1 &
    ch=$!
    sleep 0.3
    # A vehicle failure aborts the whole comparison; the log is dumped to stderr.
    if ! ./$BIN/vehicle "$@" --code "$code" --quiet --no-color >>"$log" 2>&1; then
        kill "$ch" "$tg" 2>/dev/null
        echo "run for $code failed - see $log" >&2
        cat "$log" >&2
        exit 1
    fi
    wait "$ch"
    wait "$tg"
    [ -f "$sum" ] || { echo "no summary for $code - see $log" >&2; exit 1; }
    files="$files $sum"
done

echo "=== compare ($*) ==="
# $files is intentionally unquoted so it splits into one argument per summary file; this breaks
# if a code name contains whitespace or glob characters. awk splits each line at "=", so a value
# containing "=" is cut at its second "=".
# shellcheck disable=SC2086
awk -F= '
    FNR == 1 { f++ }
    f == 1 { keys[++n] = $1 }
    { v[f, $1] = $2 }
    END {
        for (i = 1; i <= n; i++) {
            line = sprintf("%-16s", keys[i])
            for (j = 1; j <= f; j++) line = line sprintf(" %18s", v[j, keys[i]])
            print line
        }
    }' $files
