#!/usr/bin/env python3
"""Unit test for tools/dashboard.py summarize() (standard library only).

Run by `make test` (integration-test target). The dashboard's rule for a frame's final class must
match the tollgate summary: the first accepted copy decides, DETECTED_FAIL if no copy passed.
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import dashboard  # noqa: E402

failures = 0


def check(name, cond):
    global failures
    print("  %-56s %s" % (name, "ok" if cond else "FAILED"))
    if not cond:
        failures += 1


def line(seq, attempt, cls, level=1, profile="TOLL_PLAZA", lat=2.0):
    return json.dumps({"seq": seq, "attempt": attempt, "code": "hamming74", "level": level,
                       "flipped": 1, "corrected": 1, "crc_ok": cls != "DETECTED_FAIL",
                       "class": cls, "lat_us": lat, "profile": profile, "strategy": "harq"})


def main():
    empty = dashboard.summarize([])
    check("empty log -> zero frames", empty["frames"] == 0 and empty["transmissions"] == 0)
    check("empty log -> mean latency 0", empty["mean_lat_us"] == 0.0)

    lines = [
        line(0, 0, "CORRECT"),
        line(1, 0, "DETECTED_FAIL"),
        line(1, 1, "CORRECT"),            # retransmission got through -> CORRECT
        line(2, 0, "DETECTED_FAIL"),
        line(2, 1, "DETECTED_FAIL"),      # never delivered -> DETECTED_FAIL
        line(3, 0, "SILENT_WRONG"),
        line(3, 1, "CORRECT"),            # first accepted copy decides -> stays SILENT_WRONG
        "",                               # blank line ignored
        "{not json",                      # garbage line ignored (half-written line)
    ]
    s = dashboard.summarize(lines)
    check("4 distinct frames", s["frames"] == 4)
    check("7 transmissions, 3 retransmissions", s["transmissions"] == 7 and s["retransmissions"] == 3)
    check("final classes follow the tollgate rule",
          s["counts"] == {"CORRECT": 2, "DETECTED_FAIL": 1, "SILENT_WRONG": 1})
    check("one level point per frame", len(s["levels"]) == 4)
    check("recent list bounded", len(dashboard.summarize(lines, recent=3)["recent"]) == 3)
    check("missing file -> empty state", dashboard.load("/nonexistent/x.jsonl")["frames"] == 0)

    if failures:
        print("  %d failures" % failures)
        sys.exit(1)
    print("  dashboard tests passed")


if __name__ == "__main__":
    main()
