#!/usr/bin/env python3
"""Live dashboard for the FEC link (Python standard library only).

The tollgate writes one JSON object per decoded transmission with --jsonl FILE. This server
re-reads that file on every poll and serves a projector-friendly page:

    python3 tools/dashboard.py --log results/tmp/live.jsonl [--port 8050]
    python3 tools/dashboard.py --log results/tmp/live.jsonl --once     # print the state as JSON

Page: CORRECT / DETECTED_FAIL / SILENT_WRONG counters, recent frames (bits flipped by the channel
vs bits corrected by the decoder), code level over time on top of the channel profile, latency.
"""
import argparse
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CLASSES = ("CORRECT", "DETECTED_FAIL", "SILENT_WRONG")
LADDER = ["L0 uncoded", "L1 Hamming(7,4)", "L2 BCH(15,7)", "L3 BCH(31,16)",
          "L4 RS t=4", "L5 RS t=8"]


def summarize(lines, recent=40, max_levels=600):
    """Aggregate JSON lines (one per transmission) into the dashboard state.

    A frame (seq) may be transmitted several times (ARQ/HARQ/adaptive); its final class is the
    class of its first accepted copy, or DETECTED_FAIL if no copy passed the CRC."""
    final = {}            # seq -> class
    order = []            # seqs in arrival order
    tx, lat_sum = [], 0.0
    levels = []           # [seq, level, profile] at each frame's first transmission
    for raw in lines:
        raw = raw.strip()
        if not raw:
            continue
        try:
            r = json.loads(raw)
        except ValueError:
            continue
        seq, cls = r.get("seq"), r.get("class", "DETECTED_FAIL")
        tx.append(r)
        lat_sum += float(r.get("lat_us", 0.0))
        if seq not in final:
            final[seq] = cls
            order.append(seq)
            levels.append([seq, r.get("level", -1), r.get("profile", "?")])
        elif final[seq] == "DETECTED_FAIL":
            final[seq] = cls          # a retransmission got through
    counts = {c: 0 for c in CLASSES}
    for seq in order:
        if final[seq] in counts:
            counts[final[seq]] += 1
    last = tx[-1] if tx else {}
    return {
        "transmissions": len(tx),
        "frames": len(order),
        "retransmissions": len(tx) - len(order),
        "counts": counts,
        "recent": [{k: r.get(k) for k in ("seq", "attempt", "flipped", "corrected", "class", "level")}
                   for r in tx[-recent:]],
        "levels": levels[-max_levels:],
        "level": last.get("level", -1),
        "code": last.get("code", "?"),
        "profile": last.get("profile", "?"),
        "strategy": last.get("strategy", "?"),
        "mean_lat_us": lat_sum / len(tx) if tx else 0.0,
        "ladder": LADDER,
    }


def load(path, **kw):
    try:
        with open(path) as f:
            return summarize(f.readlines(), **kw)
    except OSError:
        return summarize([], **kw)


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FEC Link Live</title>
<style>
:root { color-scheme: light;
  --surface: #fcfcfb; --panel: #f3f2ef; --text: #0b0b0b; --muted: #52514e; --grid: #e4e3df;
  --flipped: #eb6834; --corrected: #2a78d6; --level: #0b0b0b;
  --toll: rgba(42,120,214,.13); --hospital: rgba(235,104,52,.16);
  --good: #0ca30c; --warning: #fab219; --critical: #d03b3b; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { color-scheme: dark;
  --surface: #1a1a19; --panel: #262624; --text: #ffffff; --muted: #c3c2b7; --grid: #3a3a37;
  --flipped: #d95926; --corrected: #3987e5; --level: #ffffff;
  --toll: rgba(57,135,229,.22); --hospital: rgba(217,89,38,.24); } }
:root[data-theme="dark"] { color-scheme: dark;
  --surface: #1a1a19; --panel: #262624; --text: #ffffff; --muted: #c3c2b7; --grid: #3a3a37;
  --flipped: #d95926; --corrected: #3987e5; --level: #ffffff;
  --toll: rgba(57,135,229,.22); --hospital: rgba(217,89,38,.24); }
* { box-sizing: border-box; }
body { margin: 0; padding: 20px 16px; background: var(--surface); color: var(--text);
  font: 18px/1.35 system-ui, -apple-system, "Segoe UI", sans-serif; }
h1 { font-size: 28px; margin: 0 0 4px; }
.sub { color: var(--muted); margin-bottom: 16px; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(170px, 1fr)); gap: 12px; }
.tile { background: var(--panel); border-radius: 10px; padding: 12px 16px; }
.tile .k { color: var(--muted); font-size: 15px; display: flex; align-items: center; gap: 8px; }
.tile .v { font-size: 44px; font-weight: 700; font-variant-numeric: tabular-nums; }
.dot { width: 14px; height: 14px; border-radius: 50%; display: inline-block; }
.charts { display: grid; grid-template-columns: 1fr; gap: 16px; margin-top: 16px; }
@media (min-width: 1100px) { .charts { grid-template-columns: 1fr 1fr; } }
.panel { background: var(--panel); border-radius: 10px; padding: 12px 16px; min-width: 0; }
.panel h2 { font-size: 19px; margin: 0 0 6px; font-weight: 600; }
.legend { color: var(--muted); font-size: 15px; display: flex; gap: 18px; flex-wrap: wrap; }
.legend span { display: inline-flex; align-items: center; gap: 6px; }
.sw { width: 14px; height: 14px; border-radius: 3px; display: inline-block; }
svg { width: 100%; height: auto; display: block; }
svg text { fill: var(--muted); font-size: 13px; }
#tip { position: fixed; pointer-events: none; background: var(--text); color: var(--surface);
  padding: 6px 10px; border-radius: 6px; font-size: 14px; display: none; }
</style></head>
<body>
<h1>Adaptive FEC link &mdash; live</h1>
<div class="sub" id="sub">waiting for the tollgate&hellip;</div>
<div class="tiles">
  <div class="tile"><div class="k"><span class="dot" style="background:var(--good)"></span>&#10004; CORRECT</div><div class="v" id="c-ok">0</div></div>
  <div class="tile"><div class="k"><span class="dot" style="background:var(--warning)"></span>&#9888; DETECTED_FAIL</div><div class="v" id="c-df">0</div></div>
  <div class="tile"><div class="k"><span class="dot" style="background:var(--critical)"></span>&#10006; SILENT_WRONG</div><div class="v" id="c-sw">0</div></div>
  <div class="tile"><div class="k">Code level now</div><div class="v" id="lvl" style="font-size:24px;padding-top:8px;overflow-wrap:break-word">&ndash;</div></div>
  <div class="tile"><div class="k">Retransmissions</div><div class="v" id="retx">0</div></div>
  <div class="tile"><div class="k">Mean decode latency</div><div class="v" id="lat">0</div></div>
</div>
<div class="charts">
  <div class="panel"><h2>Recent frames: bits flipped vs bits corrected</h2>
    <div class="legend"><span><i class="sw" style="background:var(--flipped)"></i>flipped by the channel</span>
      <span><i class="sw" style="background:var(--corrected)"></i>corrected by the decoder</span>
      <span>&#9650; = frame failed the CRC</span></div>
    <svg id="bars" viewBox="0 0 640 260" role="img" aria-label="bits flipped and corrected per recent frame"></svg></div>
  <div class="panel"><h2>Code level over time</h2>
    <div class="legend"><span><i class="sw" style="background:var(--toll)"></i>TOLL_PLAZA</span>
      <span><i class="sw" style="background:var(--hospital)"></i>HOSPITAL_IMAGING</span></div>
    <svg id="levels" viewBox="0 0 640 260" role="img" aria-label="code level per frame"></svg></div>
</div>
<div id="tip"></div>
<script>
const NS = "http://www.w3.org/2000/svg";
const tip = document.getElementById("tip");
function el(tag, attrs, parent) { const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]); parent.appendChild(e); return e; }
function hover(node, text) {
  node.addEventListener("mousemove", ev => { tip.style.display = "block"; tip.textContent = text;
    tip.style.left = (ev.clientX + 12) + "px"; tip.style.top = (ev.clientY + 12) + "px"; });
  node.addEventListener("mouseleave", () => tip.style.display = "none"); }
function bars(svg, recent) {
  svg.innerHTML = ""; const W = 640, H = 260, L = 34, B = 26, T = 10;
  const raw = Math.max(4, ...recent.map(r => Math.max(r.flipped || 0, r.corrected || 0)));
  const max = Math.ceil(raw / 4) * 4;          /* 4 gridlines at whole numbers */
  for (let g = 0; g <= 4; g++) { const y = T + (H - T - B) * (1 - g / 4);
    el("line", {x1: L, x2: W, y1: y, y2: y, stroke: "var(--grid)"}, svg);
    el("text", {x: L - 6, y: y + 4, "text-anchor": "end"}, svg).textContent = Math.round(max * g / 4); }
  const n = Math.max(recent.length, 1), slot = (W - L) / 40, bw = Math.max(2, slot / 2 - 2);
  recent.forEach((r, i) => { const x = L + i * slot + 2;
    const hf = (H - T - B) * (Math.max(r.flipped, 0) / max), hc = (H - T - B) * (r.corrected / max);
    const g = el("g", {}, svg);
    el("rect", {x: x, y: H - B - hf, width: bw, height: hf, rx: 2, fill: "var(--flipped)"}, g);
    el("rect", {x: x + bw + 1, y: H - B - hc, width: bw, height: hc, rx: 2, fill: "var(--corrected)"}, g);
    if (r.class !== "CORRECT") el("text", {x: x + bw, y: T + 12, "text-anchor": "middle",
      style: "fill:" + (r.class === "SILENT_WRONG" ? "var(--critical)" : "var(--warning)") + ";font-size:14px"}, g).textContent = "▲";
    el("rect", {x: x, y: T, width: slot, height: H - T - B, fill: "transparent"}, g);
    hover(g, `seq ${r.seq}${r.attempt ? " (retx " + r.attempt + ")" : ""}: flipped ${r.flipped}, corrected ${r.corrected}, ${r.class}`); });
  el("text", {x: W - 2, y: H - 6, "text-anchor": "end"}, svg).textContent = "last " + recent.length + " transmissions →";
}
function levels(svg, pts, ladder) {
  svg.innerHTML = ""; const W = 640, H = 260, L = 128, B = 22, T = 8, nl = ladder.length;
  const yOf = l => T + (H - T - B) * (1 - (l + 0.5) / nl);
  if (!pts.length) return;
  const x0 = pts[0][0], x1 = Math.max(pts[pts.length - 1][0], x0 + 1);
  const xOf = s => L + (W - L - 4) * (s - x0) / (x1 - x0);
  let start = 0;
  for (let i = 1; i <= pts.length; i++) if (i === pts.length || pts[i][2] !== pts[start][2]) {
    const prof = pts[start][2]; el("rect", {x: xOf(pts[start][0]), y: T, height: H - T - B,
      width: Math.max(1, xOf(i < pts.length ? pts[i][0] : x1) - xOf(pts[start][0])),
      fill: prof === "HOSPITAL_IMAGING" ? "var(--hospital)" : "var(--toll)"}, svg); start = i; }
  ladder.forEach((name, l) => { el("line", {x1: L, x2: W, y1: yOf(l), y2: yOf(l), stroke: "var(--grid)"}, svg);
    el("text", {x: L - 6, y: yOf(l) + 4, "text-anchor": "end"}, svg).textContent = name; });
  let d = ""; pts.forEach((p, i) => { const x = xOf(p[0]), y = yOf(Math.max(p[1], 0));
    d += i ? ` H${x} V${y}` : `M${x} ${y}`; });
  el("path", {d: d, fill: "none", stroke: "var(--level)", "stroke-width": 2.5}, svg);
  el("text", {x: L, y: H - 4}, svg).textContent = "frame " + x0;
  el("text", {x: W - 2, y: H - 4, "text-anchor": "end"}, svg).textContent = "frame " + x1;
}
async function tick() {
  try { const s = await (await fetch("api/state", {cache: "no-store"})).json();
    document.getElementById("c-ok").textContent = s.counts.CORRECT;
    document.getElementById("c-df").textContent = s.counts.DETECTED_FAIL;
    document.getElementById("c-sw").textContent = s.counts.SILENT_WRONG;
    document.getElementById("lvl").textContent = s.level >= 0 ? s.ladder[s.level] : s.code;
    document.getElementById("retx").textContent = s.retransmissions;
    document.getElementById("lat").textContent = s.mean_lat_us.toFixed(1) + " µs";
    document.getElementById("sub").textContent = `${s.frames} frames, ${s.transmissions} transmissions — strategy ${s.strategy}, channel now ${s.profile}`;
    bars(document.getElementById("bars"), s.recent);
    levels(document.getElementById("levels"), s.levels, s.ladder);
  } catch (e) { document.getElementById("sub").textContent = "dashboard server not reachable"; }
}
tick(); setInterval(tick, 500);
</script></body></html>
"""


def make_handler(log_path):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/api/state"):
                body, ctype = json.dumps(load(log_path)).encode(), "application/json"
            elif self.path in ("/", "/index.html"):
                body, ctype = PAGE.encode(), "text/html; charset=utf-8"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):  # keep the demo terminal clean
            pass
    return Handler


def main():
    ap = argparse.ArgumentParser(description="Live FEC link dashboard")
    ap.add_argument("--log", default="results/tmp/live.jsonl", help="tollgate --jsonl file")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8050)
    ap.add_argument("--once", action="store_true", help="print the current state as JSON and exit")
    a = ap.parse_args()
    if a.once:
        print(json.dumps(load(a.log), indent=1))
        return
    server = ThreadingHTTPServer((a.host, a.port), make_handler(a.log))
    print(f"[DASHBOARD] open http://{a.host}:{a.port}/   (reading {a.log}, Ctrl-C to stop)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    sys.exit(main())
