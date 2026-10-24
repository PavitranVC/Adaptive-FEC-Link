#!/bin/sh
# Starts the live dashboard (http://127.0.0.1:8050/ by default, PORT=... to change) and then the
# one-terminal demo with the given flags, the tollgate writing results/tmp/live.jsonl.
# The dashboard keeps running after the demo so the final state stays on screen; Ctrl-C stops it.
#
#   PORT=8051 sh tools/dashboard.sh --profile toll --strategy adaptive --count 200 [more flags]
#
# Flags are forwarded to tools/demo.sh; --jsonl is appended after them, so a caller's --jsonl
# is overridden. PORT is passed to dashboard.py (a bad or busy one makes it exit 1, and this script
# stop) and the page is bound to 127.0.0.1
# (dashboard.py's default --host). The demo's exit status is not examined (a failed demo still
# prints "demo finished"). The script's own status is dashboard.py's (from the final "wait"),
# or 0 after Ctrl-C/TERM, which the trap treats as a normal stop.
cd "$(dirname "$0")/.." || exit 1
mkdir -p results/tmp
PORT=${PORT:-8050}
# Truncate first so the dashboard never shows a previous run; the tollgate reopens the file
# with "w" when it starts, so this only covers the window before that.
: >results/tmp/live.jsonl
python3 tools/dashboard.py --log results/tmp/live.jsonl --port "$PORT" &
DB=$!
trap 'kill $DB 2>/dev/null; exit 0' INT TERM
# The dashboard is given 0.5 s to bind its port. If it has exited by then (e.g. port in use or
# out of range) its error is already printed, so stop instead of running the demo without it.
sleep 0.5
kill -0 $DB 2>/dev/null || { echo "dashboard failed to start - not starting the demo" >&2; exit 1; }
echo "=== dashboard: open http://127.0.0.1:$PORT/ (starting the demo in 3 s) ==="
sleep 3
sh tools/demo.sh "$@" --jsonl results/tmp/live.jsonl
echo "=== demo finished - dashboard still at http://127.0.0.1:$PORT/  (Ctrl-C to stop) ==="
wait $DB
