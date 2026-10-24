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
# Exit status: 0, or 1 if a vehicle run fails or a summary file was not produced.
cd "$(dirname "$0")/.." || exit 1
BIN=${BIN:-bin}      # make sanitize sets BIN=build/asan/bin
mkdir -p results/tmp
CODES=${CODES:-"hamming74 bch157"}

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
