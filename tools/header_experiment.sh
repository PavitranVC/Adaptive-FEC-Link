#!/bin/sh
# Header-corruption experiment (decision #1): what does an UNPROTECTED header cost?
#
#   sh tools/header_experiment.sh [FRAMES]        -> results/header_corruption.csv
#
# Runs the real three-process pipeline (toll profile, RS t=4, seed 42) with
# `channel --corrupt-header P` for P = 0, 0.01, 0.05, 0.2 and strategies fec and harq.
# A reference run without corruption gives the true tag of every seq. Each SENT frame is then
# classified from the tollgate's per-transmission log (tag_true comes from the channel's
# sidecar, which is never corrupted):
#   delivered   a CORRECT copy arrived under its own seq
#   failed      copies arrived under its own seq, none CORRECT (e.g. corrupted interleaver depth)
#   misrouted   its tag only arrived under a WRONG seq (corrupted seq field)
#   lost        its tag never reached the decoder (dropped: malformed / invalid / "duplicate")
# plus: phantom = log rows whose seq does not belong to their tag, reported_correct = what the
# tollgate summary claims (it cannot know about misrouting).
cd "$(dirname "$0")/.." || exit 1
N=${1:-2000}
mkdir -p results/tmp
OUT=results/header_corruption.csv
PORTS="--channel-port 19400 --tollgate-port 19401 --feedback-port 19402"
COMMON="$PORTS --no-color --quiet --profile toll --code rs --seed 42 --count $N --delay-ms 0 --idle-timeout-ms 3000"
HARQ="--strategy harq --rtt-ms 1 --timeout-ms 20"

run() { # run <tag> <flags...> -> results/tmp/hdr_<tag>.csv + .sum
    tag=$1; shift
    sh tools/demo.sh $COMMON "$@" >results/tmp/hdr_$tag.out 2>&1 || { echo "run $tag failed" >&2; exit 1; }
    cp results/tmp/tollgate.csv results/tmp/hdr_$tag.csv
    cp results/tmp/summary.txt results/tmp/hdr_$tag.sum
}

run ref
echo "strategy,p_header,frames,delivered,failed,misrouted,lost,phantom_rows,hdr_corrupted,tollgate_malformed,tollgate_invalid,tollgate_duplicates,reported_correct" >$OUT
for strat in fec harq; do
    for p in 0 0.01 0.05 0.2; do
        flags=""
        [ "$strat" = harq ] && flags=$HARQ
        run "${strat}_$p" $flags --corrupt-header $p
        hc=$(sed -n 's/.*header corrupted in \([0-9]*\) of.*/\1/p' results/tmp/hdr_${strat}_$p.out)
        awk -F, -v N="$N" -v strat="$strat" -v p="$p" -v hc="${hc:-0}" \
            -v sum="results/tmp/hdr_${strat}_$p.sum" '
            function sval(k,   line, kv) {
                while ((getline line < sum) > 0) { split(line, kv, "="); S[kv[1]] = kv[2] }
                close(sum); return (k in S) ? S[k] : 0
            }
            FNR == 1 { f++; next }
            f == 1 { tag_of[$1] = $8; seq_of[$8] = $1; next }          # reference: seq -> tag
            {                                                          # corrupted run, 1 row/tx
                s = $1; t = $8
                if (!(t in seq_of)) { phantom++; next }
                own = seq_of[t]
                if (s != own) { phantom++; wrongseq[own] = 1; next }
                seen[own] = 1
                if ($6 == "CORRECT") ok[own] = 1
            }
            END {
                for (i = 0; i < N; i++) {
                    if (ok[i]) d++
                    else if (seen[i]) fl++
                    else if (wrongseq[i]) m++
                    else l++
                }
                printf "%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", strat, p, N, d, fl, m, l, phantom,
                       hc, sval("malformed"), sval("invalid"), sval("duplicates"), sval("correct")
            }' results/tmp/hdr_ref.csv results/tmp/hdr_${strat}_$p.csv >>$OUT
    done
done
cat $OUT
