#!/usr/bin/env python3
"""Plots for the FEC benchmark.

usage: python3 tools/plot.py results/bench.csv [--suffix _hospital] [--outdir results]
       python3 tools/plot.py results/bench_strategy.csv    (strategy comparison, week 2)
       python3 tools/plot.py results/adaptive_trace.csv    (adaptive code level over time)

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
          "rs": "RS(24,16) t=4"}
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


def base_code(code):
    """'hamming74+il8' -> ('hamming74', 8); plain codes -> (code, 1)."""
    if "+il" in code:
        b, d = code.split("+il", 1)
        return b, int(d)
    return code, 1


def codes_in(rows):
    present = {r["code"] for r in rows}
    order = {c: i for i, c in enumerate(CODE_ORDER)}
    return sorted(present, key=lambda c: (order.get(base_code(c)[0], 99), base_code(c)[1], c))


def color_of(code):
    return COLORS.get(base_code(code)[0], MUTED)


def pretty(code):
    b, d = base_code(code)
    return LABELS.get(b, b) + (f" + interleaver d={d}" if d > 1 else "")


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


def short(code):
    b, d = base_code(code)
    return LABELS.get(b, b) + (f"\n+ il d={d}" if d > 1 else "")


def label_of(code, rows):
    rate = next(r["code_rate"] for r in rows if r["code"] == code)
    return f"{pretty(code)}  (rate {rate:.2f})"


def sweep_plot(rows, model, xlabel, title, out, logx):
    sel = [r for r in rows if r["model"] == model]
    if not sel:
        return None
    fig, ax = plt.subplots(figsize=(12, 5.5), facecolor=SURFACE)
    for code in codes_in(sel):
        pts = sorted((r["param"], r["frame_success_rate"]) for r in sel if r["code"] == code)
        dashed = base_code(code)[1] > 1   # interleaved variant: same colour, dashed line
        ax.plot([p for p, _ in pts], [s for _, s in pts], color=color_of(code),
                marker=MARKERS.get(base_code(code)[0], "o"), markersize=6, linewidth=2,
                linestyle="--" if dashed else "-", markerfacecolor=SURFACE if dashed else None,
                label=label_of(code, sel))
    if logx:
        ax.set_xscale("log")
    ax.set_ylim(-0.02, 1.02)
    style(ax, title[: title.index(" [")], xlabel + "\n" + title[title.index(" [") + 2:-1],
          "frame success rate (CORRECT / frames)")
    ax.legend(frameon=False, fontsize=9, labelcolor=TEXT, loc="upper left", bbox_to_anchor=(1.01, 1.0))
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


def bar_plot(rows, values, title, ylabel, out, fmt):
    codes = codes_in(rows)
    vals = [values(code) for code in codes]
    fig, ax = plt.subplots(figsize=(10, 5), facecolor=SURFACE)
    bars = ax.bar([short(c) for c in codes], vals,
                  color=[color_of(c) for c in codes], width=0.6,
                  edgecolor=SURFACE, linewidth=2,
                  hatch=["//" if base_code(c)[1] > 1 else "" for c in codes])
    for b, v in zip(bars, vals):
        ax.annotate(fmt(v), (b.get_x() + b.get_width() / 2, b.get_height()),
                    ha="center", va="bottom", fontsize=9, color=TEXT,
                    xytext=(0, 3), textcoords="offset points")
    style(ax, title, "", ylabel)
    ax.set_ylim(0, max(vals) * 1.15 if vals and max(vals) > 0 else 1)
    ax.tick_params(axis="x", labelsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


CHANNEL_COLORS = {"TOLL_PLAZA": PALETTE[0], "HOSPITAL_IMAGING": PALETTE[1]}


def channel_color(ch):
    return CHANNEL_COLORS.get(ch, PALETTE[2])


FAMILY_COLORS = {"fec": PALETTE[0], "arq": PALETTE[7], "harq": PALETTE[2], "adaptive": PALETTE[6]}


def goodput_plot(rows, outdir):
    """Goodput (useful payload bits per transmitted bit) vs success rate, one panel per channel."""
    channels = []
    for r in rows:
        if r["channel"] not in channels:
            channels.append(r["channel"])
    fig, axes = plt.subplots(1, len(channels), figsize=(6 * len(channels), 5.6), facecolor=SURFACE,
                             sharey=True)
    if len(channels) == 1:
        axes = [axes]
    for ax, ch in zip(axes, channels):
        pts = [r for r in rows if r["channel"] == ch]
        span = max(r["success_rate"] for r in pts) - min(r["success_rate"] for r in pts) or 1.0
        groups = []                     # points closer than ~2% of the axes share one label
        for r in pts:
            fam = r["strategy"]
            ax.plot(r["success_rate"], r["goodput"], marker="D" if fam == "adaptive" else "o",
                    markersize=11 if fam == "adaptive" else 8, linestyle="none",
                    color=FAMILY_COLORS.get(fam, MUTED), markeredgecolor=SURFACE, markeredgewidth=1.5)
            name = "ADAPTIVE" if fam == "adaptive" else f"{fam} {r['code']}"
            for g in groups:
                if abs(g[0] - r["success_rate"]) < 0.1 * span and abs(g[1] - r["goodput"]) < 0.035:
                    g[2].append(name)
                    break
            else:
                groups.append([r["success_rate"], r["goodput"], [name]])
        for x, y, names in groups:
            ax.annotate("\n".join(sorted(names, key=lambda n: -next(
                r["goodput"] for r in pts if ("ADAPTIVE" if r["strategy"] == "adaptive" else f"{r['strategy']} {r['code']}") == n))), (x, y), fontsize=8, color=TEXT, va="center",
                        xytext=(-8, 0), textcoords="offset points", ha="right")
        style(ax, ch.replace("schedule", "scheduled channel (toll -> hospital -> toll)"),
              "frame success rate", "goodput (payload bits / transmitted bits)" if ax is axes[0] else "")
        lo = min(r["success_rate"] for r in pts)
        ax.set_xlim(max(0.0, lo - 0.05), 1.03)
    for fam, col in FAMILY_COLORS.items():
        axes[-1].plot([], [], marker="D" if fam == "adaptive" else "o", linestyle="none", color=col, label=fam)
    axes[-1].legend(frameon=False, fontsize=9, loc="upper left", labelcolor=TEXT)
    fig.suptitle("Goodput vs reliability: top-right is best (parity and retransmissions both cost goodput)",
                 color=TEXT, x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    out = os.path.join(outdir, "goodput_vs_success.png")
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


def strategy_plot(path, outdir):
    """Strategy comparison: success rate, latency (mean bar + p99 marker), retransmissions."""
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("success_rate", "retx_per_frame", "mean_latency_ms", "p99_latency_ms", "mean_code_rate"):
            r[k] = float(r[k])
        r["goodput"] = float(r.get("goodput") or 0.0)
    labels, seen = [], set()
    for r in rows:
        key = (r["strategy"], r["code"])
        if key not in seen:
            seen.add(key)
            labels.append(key)
    channels = []
    for r in rows:
        if r["channel"] not in channels:
            channels.append(r["channel"])
    rs_label = {"rs_t4": "RS t=4", "rs_t8": "RS t=8"}
    pretty_row = lambda k: k[0] + ("" if k[0] in ("arq", "adaptive") else " " + rs_label.get(k[1], LABELS.get(k[1], k[1])))
    fig, axes = plt.subplots(1, 3, figsize=(15, 0.42 * len(labels) * len(channels) / 2 + 2.6),
                             facecolor=SURFACE, sharey=True)
    h = 0.8 / len(channels)
    for ci, ch in enumerate(channels):
        by = {(r["strategy"], r["code"]): r for r in rows if r["channel"] == ch}
        ys = [i + (ci - (len(channels) - 1) / 2) * h for i in range(len(labels))]
        get = lambda k, field: by[k][field] if k in by else float("nan")
        col = channel_color(ch)
        axes[0].barh(ys, [get(k, "success_rate") for k in labels], height=h, color=col,
                     edgecolor=SURFACE, linewidth=1, label=ch)
        axes[1].barh(ys, [get(k, "mean_latency_ms") for k in labels], height=h, color=col,
                     edgecolor=SURFACE, linewidth=1, label=f"{ch} mean")
        axes[1].plot([get(k, "p99_latency_ms") for k in labels], ys, linestyle="none", marker="|",
                     markersize=9, markeredgewidth=2, color=TEXT if ci == 0 else MUTED,
                     label=f"{ch} p99")
        axes[2].barh(ys, [get(k, "retx_per_frame") for k in labels], height=h, color=col,
                     edgecolor=SURFACE, linewidth=1)
    axes[0].set_yticks(range(len(labels)))
    axes[0].set_yticklabels([pretty_row(k) for k in labels], fontsize=9)
    axes[0].invert_yaxis()
    style(axes[0], "Frame success rate", "CORRECT / frames", "")
    axes[0].set_xlim(0, 1.05)
    style(axes[1], "Delivery latency (bar = mean, | = p99)", "milliseconds (simulated clock)", "")
    style(axes[2], "Retransmissions per frame", "transmissions / frame - 1", "")
    handles, names = axes[1].get_legend_handles_labels()
    fig.legend(handles, names, frameon=False, fontsize=8, loc="upper right", ncol=2 * len(channels),
               labelcolor=TEXT)
    fig.suptitle("Strategy comparison: FEC vs ARQ vs Hybrid ARQ vs adaptive  "
                 f"[{rows[0]['frames']} frames per bar]", color=TEXT, x=0.01, ha="left", y=0.995)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    out = os.path.join(outdir, "strategy_comparison.png")
    fig.savefig(out, dpi=130)
    plt.close(fig)
    if "goodput" in rows[0]:
        print("wrote", goodput_plot(rows, outdir))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--suffix", default="")
    ap.add_argument("--outdir", default="results")
    a = ap.parse_args()
    with open(a.csv) as f:
        header = f.readline()
    os.makedirs(a.outdir, exist_ok=True)
    if header.startswith("strategy,"):
        print("wrote", strategy_plot(a.csv, a.outdir))
        return
    if header.startswith("frame,tx,segment,level"):
        print("wrote", trace_plot(a.csv, a.outdir))
        return
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
    fig, ax = plt.subplots(figsize=(11, 6), facecolor=SURFACE)
    x = list(range(len(codes)))
    w = 0.38
    cr = [caught[c] / frames[c] for c in codes]
    ax.bar([i - w / 2 for i in x], cr, width=w, color=[color_of(c) for c in codes],
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
    ax.set_xticklabels([short(c) for c in codes], rotation=25, ha="right")
    style(ax, "Failures caught by CRC vs silent errors (all sweep points pooled)",
          tag.strip(" []"), "fraction of frames (log scale)")
    ax.legend(frameon=False, fontsize=8, labelcolor=TEXT, loc="upper center",
              bbox_to_anchor=(0.5, -0.22), ncol=2)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


LADDER = ["L0 uncoded", "L1 Hamming(7,4)", "L2 BCH(15,7)", "L3 BCH(31,16)",
          "L4 RS(24,16) t=4", "L5 RS(32,16) t=8"]


def trace_plot(path, outdir):
    """Adaptive code level per frame, on top of the channel profile (shaded) + failed frames."""
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    frames = [int(r["frame"]) for r in rows]
    levels = [int(r["level"]) for r in rows]
    fig, ax = plt.subplots(figsize=(12, 4.8), facecolor=SURFACE)
    # shade the channel segments
    start, seg = 0, rows[0]["segment"]
    shade = {"TOLL_PLAZA": PALETTE[0], "HOSPITAL_IMAGING": PALETTE[1]}
    spans = []
    for i, r in enumerate(rows + [{"segment": None}]):
        if r["segment"] != seg:
            spans.append((start, i, seg))
            start, seg = i, r["segment"]
    for a, b, name in spans:
        ax.axvspan(frames[a], frames[b - 1] + 1, color=shade.get(name, MUTED), alpha=0.12, linewidth=0)
        ax.text((frames[a] + frames[b - 1]) / 2, len(LADDER) - 0.35, name, ha="center", va="top",
                fontsize=9, color=MUTED)
    ax.step(frames, levels, where="post", color=TEXT, linewidth=2, label="code level")
    fails = [int(r["frame"]) for r in rows if r["class"] != "CORRECT"]
    ax.plot(fails, [-0.35] * len(fails), linestyle="none", marker="|", markersize=10,
            color=PALETTE[7], label=f"frame not delivered correctly ({len(fails)})")
    ax.set_yticks(range(len(LADDER)))
    ax.set_yticklabels(LADDER, fontsize=9)
    ax.set_ylim(-0.7, len(LADDER) - 0.2)
    style(ax, "Adaptive FEC: code level follows the interference", "frame", "")
    ax.legend(frameon=False, fontsize=8, loc="center right", labelcolor=TEXT)
    fig.tight_layout()
    out = os.path.join(outdir, "adaptive_level.png")
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


if __name__ == "__main__":
    main()
