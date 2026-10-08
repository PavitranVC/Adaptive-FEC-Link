#!/bin/sh
# Runs the full UDP pipeline once per code with the SAME seed and flags and prints the
# tollgate summaries side by side.
#
#   CODES="hamming74 bch157" sh tools/compare.sh --seed 42 --count 500 --profile toll
#
# Default CODES: "hamming74 bch157". Extra flags are passed to all three programs.
cd "$(dirname "$0")/.." || exit 1
mkdir -p results/tmp
CODES=${CODES:-"hamming74 bch157"}

files=""
for code in $CODES; do
    sum="results/tmp/compare_$code.txt"
    log="results/tmp/compare_$code.log"
    rm -f "$sum"
    ./bin/tollgate "$@" --code "$code" --quiet --no-color --summary-file "$sum" >"$log" 2>&1 &
    tg=$!
    sleep 0.3
    ./bin/channel "$@" --code "$code" --quiet --no-color >>"$log" 2>&1 &
    ch=$!
    sleep 0.3
    if ! ./bin/vehicle "$@" --code "$code" --quiet --no-color >>"$log" 2>&1; then
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
