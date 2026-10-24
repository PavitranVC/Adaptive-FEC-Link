#!/bin/bash
# CN viva demo, start to finish (see docs/DEMO.md).
#   tools/present.sh [--nopause] [--check]
# --nopause: no Enter pauses.  --check: preflight only.
cd "$(dirname "$0")/.." || exit 1
PAUSE=1; CHECK=0
for a in "$@"; do case $a in --nopause) PAUSE=0;; --check) CHECK=1;; *) echo "usage: $0 [--nopause] [--check]"; exit 2;; esac; done
PIDS=(); PGS=(); FAILED=0
cleanup() {
    trap - EXIT INT TERM
    for g in "${PGS[@]}"; do kill -TERM -- "-$g" 2>/dev/null; done
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
    sudo pkill -INT -x tshark 2>/dev/null
    pkill -f "bin/(vehicle|channel|tollgate)" 2>/dev/null
}
trap cleanup EXIT
trap 'echo; echo "interrupted"; exit 130' INT TERM
banner() { echo; echo "=== $1 ==="; [ $PAUSE = 1 ] && read -r -p "[Enter to run] " _; }
result() { if [ "$2" = 0 ]; then echo "STEP $1: PASS"; else echo "STEP $1: FAIL"; FAILED=1; fi; }

# 1. preflight
banner "1. Preflight"
rc=0
for t in gcc make python3 tshark; do
    if command -v $t >/dev/null; then echo "PASS  $t present"; else echo "FAIL  $t missing"; rc=1; fi
done
if pgrep -fl "bin/(vehicle|channel|tollgate)" || ss -H -lun 2>/dev/null | grep -qE ':900[0-2]\b' \
   || ss -H -ltn 2>/dev/null | grep -q ':8050\b'; then
    echo "FAIL  UDP 9000-9002 / TCP 8050 in use. List old runs: pgrep -fl \"bin/(vehicle|channel|tollgate)\"  then kill <pid>"
    result 1 1; exit 1
fi
echo "PASS  ports 9000-9002 (UDP) and 8050 (TCP) free"
[ $rc = 0 ] && { (make && make test) 2>&1 | tail -3; [ "${PIPESTATUS[0]}" = 0 ] || rc=1; }
result 1 $rc
[ $rc = 0 ] || exit 1
[ $CHECK = 1 ] && exit 0

# 2. three processes + packet capture
banner "2. Three-process demo with tshark capture"
mkdir -p results/tmp; rm -f results/tmp/wire.pcap
sudo tshark -i lo -f "udp portrange 9000-9002" -w /tmp/wire.$$.pcap >/dev/null 2>&1 & TP=$!
sleep 4
./bin/tollgate --profile toll --code hamming74 --show-bits & PIDS+=($!)
sleep 0.5
./bin/channel --profile toll --model bsc --p 0.01 --seed 42 & PIDS+=($!)
sleep 0.5
./bin/vehicle --profile toll --code hamming74 --seed 42 --count 20 --delay-ms 500 & VP=$!; PIDS+=($VP)
wait $VP; rc=$?
sleep 1
kill "${PIDS[@]}" 2>/dev/null; sudo pkill -INT -x tshark 2>/dev/null; wait $TP 2>/dev/null; PIDS=()
sudo chown "$USER" /tmp/wire.$$.pcap 2>/dev/null; mv /tmp/wire.$$.pcap results/tmp/wire.pcap
echo "--- packets per destination port ---"
tshark -r results/tmp/wire.pcap -T fields -e udp.dstport 2>/dev/null | sort | uniq -c
[ -n "$(tshark -r results/tmp/wire.pcap 2>/dev/null | head -1)" ] || rc=1
result 2 $rc

# 3. adaptive FEC
banner "3. Adaptive FEC (make demo-adaptive)"
make demo-adaptive; result 3 $?

# 4. dashboard
banner "4. Live dashboard"
setsid make dashboard >results/tmp/dashboard.log 2>&1 & DP=$!; PGS+=($DP)
sleep 3
echo "Open: http://127.0.0.1:${PORT:-8050}/"
if [ $PAUSE = 1 ]; then read -r -p "[Enter to stop the dashboard] " _; else sleep 5; fi
kill -TERM -- "-$DP" 2>/dev/null; PGS=()
pkill -f "bin/(vehicle|channel|tollgate)" 2>/dev/null
result 4 0

# 5. comparison
banner "5. hamming74 vs bch157 (make compare)"
make compare; result 5 $?

# 6. plots
banner "6. Committed plots"
ls results/*.png; result 6 $?
[ $FAILED = 0 ] && echo "ALL STEPS PASSED" || echo "SOME STEPS FAILED"
exit $FAILED
