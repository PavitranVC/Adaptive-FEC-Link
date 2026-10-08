#!/usr/bin/env python3
"""Plots for the FEC benchmark.

usage: python3 tools/plot.py results/bench.csv [--suffix _hospital] [--outdir results]

Writes PNGs into results/:
  success_vs_p_bsc<suffix>.png      frame success rate vs flip probability p (BSC), per code
  success_vs_pgb_ge<suffix>.png     frame success rate vs burst-start probability (Gilbert-Elliott)
  success_vs_burst_len<suffix>.png  frame success rate vs burst length L (one burst per frame)
  silent_wrong<suffix>.png          silent-wrong rate per code (CRC passed, ID wrong)
  latency<suffix>.png               mean decode latency per code
"""
import argparse
import csv
import os
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib is missing: pip install -r requirements.txt")

# Fixed colour per code (identity follows the code, never its rank) - validated categorical
# palette, light mode, in slot order.
CODE_ORDER = ["none", "hamming74", "hamming1511", "secded84", "bch157", "bch3116", "rs"]
PALETTE = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
COLORS = {c: PALETTE[i] for i, c in enumerate(CODE_ORDER)}
MARKERS = {"none": "o", "hamming74": "s", "hamming1511": "D", "secded84": "^",
           "bch157": "v", "bch3116": "P", "rs": "X"}
LABELS = {"none": "uncoded", "hamming74": "Hamming(7,4)", "hamming1511": "Hamming(15,11)",
          "secded84": "SECDED(8,4)", "bch157": "BCH(15,7)", "bch3116": "BCH(31,16)",
          "rs": "Reed-Solomon"}
TEXT, MUTED, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"


def load(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            for k in ("param", "frame_success_rate", "detected_fail_rate", "silent_wrong_rate",
                      "code_rate", "mean_decode_us"):
                r[k] = float(r[k])
            r["frames"] = int(r["frames"])
            rows.append(r)
    return rows


def codes_in(rows):
    present = {r["code"] for r in rows}
    return [c for c in CODE_ORDER if c in present] + sorted(present - set(CODE_ORDER))


def style(ax, title, xlabel, ylabel):
    ax.set_facecolor(SURFACE)
    ax.set_title(title, color=TEXT, fontsize=12, loc="left")
    ax.set_xlabel(xlabel, color=MUTED)
    ax.set_ylabel(ylabel, color=MUTED)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)
    ax.tick_params(colors=MUTED)


def label_of(code, rows):
    rate = next(r["code_rate"] for r in rows if r["code"] == code)
    return f"{LABELS.get(code, code)}  (rate {rate:.2f})"


def sweep_plot(rows, model, xlabel, title, out, logx):
    sel = [r for r in rows if r["model"] == model]
    if not sel:
        return None
    fig, ax = plt.subplots(figsize=(8, 5), facecolor=SURFACE)
    for code in codes_in(sel):
        pts = sorted((r["param"], r["frame_success_rate"]) for r in sel if r["code"] == code)
        ax.plot([p for p, _ in pts], [s for _, s in pts], color=COLORS.get(code, MUTED),
                marker=MARKERS.get(code, "o"), markersize=6, linewidth=2,
                label=label_of(code, sel))
    if logx:
        ax.set_xscale("log")
    ax.set_ylim(-0.02, 1.02)
    style(ax, title[: title.index(" [")], xlabel + "\n" + title[title.index(" [") + 2:-1],
          "frame success rate (CORRECT / frames)")
    ax.legend(frameon=False, fontsize=9, labelcolor=TEXT)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


def bar_plot(rows, values, title, ylabel, out, fmt):
    codes = codes_in(rows)
    vals = [values(code) for code in codes]
    fig, ax = plt.subplots(figsize=(8, 4.5), facecolor=SURFACE)
    bars = ax.bar([LABELS.get(c, c) for c in codes], vals,
                  color=[COLORS.get(c, MUTED) for c in codes], width=0.6,
                  edgecolor=SURFACE, linewidth=2)
    for b, v in zip(bars, vals):
        ax.annotate(fmt(v), (b.get_x() + b.get_width() / 2, b.get_height()),
                    ha="center", va="bottom", fontsize=9, color=TEXT,
                    xytext=(0, 3), textcoords="offset points")
    style(ax, title, "", ylabel)
    ax.set_ylim(0, max(vals) * 1.15 if vals and max(vals) > 0 else 1)
    ax.tick_params(axis="x", labelrotation=20)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--suffix", default="")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()
    rows = load(a.csv)
    if not rows:
        sys.exit(f"{a.csv} is empty - run 'make bench' first")
    os.makedirs(a.outdir, exist_ok=True)
    frames = rows[0]["frames"]
    tag = f" [{a.suffix.strip('_') or 'toll'} profile, {frames} frames/point]"
    path = lambda name: os.path.join(a.outdir, f"{name}{a.suffix}.png")
    written = [
        sweep_plot(rows, "bsc", "bit flip probability p (log scale)",
                   "Success rate vs p - binary symmetric channel" + tag, path("success_vs_p_bsc"), True),
        sweep_plot(rows, "ge", "p_gb: probability per bit that a burst starts (log scale)",
                   "Success rate - Gilbert-Elliott bursty channel" + tag, path("success_vs_pgb_ge"), True),
        sweep_plot(rows, "burst", "burst length L (bits), one burst per frame",
                   "Success rate vs burst length" + tag, path("success_vs_burst_len"), False),
    ]
    written.append(silent_plot(rows, path("silent_wrong"), tag))
    lat = defaultdict(list)
    for r in rows:
        lat[r["code"]].append(r["mean_decode_us"])
    written.append(bar_plot(
        rows, lambda c: sum(lat[c]) / len(lat[c]),
        "Mean decode latency per frame (FEC decode + CRC check)", "microseconds",
        path("latency"), lambda v: f"{v:.1f} us"))
    for w in written:
        if w:
            print("wrote", w)


def silent_plot(rows, out, tag):
    """Per code, pooled over every sweep point: frames where FEC delivered a wrong/undecodable
    payload that the CRC CAUGHT (detected_fail) vs frames that slipped through (silent_wrong).
    Log scale; a zero count is drawn as an upper bound '< 1/N' marker."""
    codes = codes_in(rows)
    caught, silent, frames = defaultdict(float), defaultdict(float), defaultdict(int)
    for r in rows:
        caught[r["code"]] += r["detected_fail_rate"] * r["frames"]
        silent[r["code"]] += r["silent_wrong_rate"] * r["frames"]
        frames[r["code"]] += r["frames"]
    fig, ax = plt.subplots(figsize=(9, 5.6), facecolor=SURFACE)
    x = list(range(len(codes)))
    w = 0.38
    cr = [caught[c] / frames[c] for c in codes]
    ax.bar([i - w / 2 for i in x], cr, width=w, color=[COLORS.get(c, MUTED) for c in codes],
           edgecolor=SURFACE, linewidth=2, label="DETECTED_FAIL (caught by CRC-32)")
    floor = min(1.0 / frames[c] for c in codes)
    for i, c in enumerate(codes):
        v = silent[c] / frames[c]
        ax.annotate(f"{cr[i]:.3f}", (i - w / 2, cr[i]), ha="center", va="bottom", fontsize=8,
                    color=TEXT, xytext=(0, 3), textcoords="offset points")
        if v > 0:
            ax.bar(i + w / 2, v, width=w, color=PALETTE[7], edgecolor=SURFACE, linewidth=2)
            ax.annotate(f"{v:.1e}", (i + w / 2, v), ha="center", va="bottom", fontsize=8,
                        color=TEXT, xytext=(0, 3), textcoords="offset points")
        else:
            ax.plot(i + w / 2, 1.0 / frames[c], marker="v", color=PALETTE[7], markersize=8)
            ax.annotate(f"0 seen\n< {1.0 / frames[c]:.0e}", (i + w / 2, 1.0 / frames[c]),
                        ha="center", va="bottom", fontsize=8, color=TEXT,
                        xytext=(0, 6), textcoords="offset points")
    ax.plot([], [], marker="v", color=PALETTE[7], linestyle="none",
            label="SILENT_WRONG (CRC passed, ID wrong); marker = upper bound when 0 observed")
    ax.set_yscale("log")
    ax.set_ylim(floor / 3, 2.0)
    ax.set_xticks(x)
    ax.set_xticklabels([LABELS.get(c, c) for c in codes], rotation=15)
    style(ax, "Failures caught by CRC vs silent errors (all sweep points pooled)",
          tag.strip(" []"), "fraction of frames (log scale)")
    ax.legend(frameon=False, fontsize=8, labelcolor=TEXT, loc="upper center",
              bbox_to_anchor=(0.5, -0.22), ncol=2)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


if __name__ == "__main__":
    main()
