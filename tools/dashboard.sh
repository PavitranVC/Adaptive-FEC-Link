#!/bin/sh
# Starts the live dashboard (http://127.0.0.1:8050/ by default, PORT=... to change) and then the
# one-terminal demo with the given flags, the tollgate writing results/tmp/live.jsonl.
# The dashboard keeps running after the demo so the final state stays on screen; Ctrl-C stops it.
cd "$(dirname "$0")/.." || exit 1
mkdir -p results/tmp
PORT=${PORT:-8050}
: >results/tmp/live.jsonl
python3 tools/dashboard.py --log results/tmp/live.jsonl --port "$PORT" &
DB=$!
trap 'kill $DB 2>/dev/null; exit 0' INT TERM
sleep 0.5
echo "=== dashboard: open http://127.0.0.1:$PORT/ (starting the demo in 3 s) ==="
sleep 3
sh tools/demo.sh "$@" --jsonl results/tmp/live.jsonl
echo "=== demo finished - dashboard still at http://127.0.0.1:$PORT/  (Ctrl-C to stop) ==="
wait $DB
