"""Decode Rigol BYTE records using recorded preambles; preserve measured volts."""
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

p = Path(__file__).resolve().parent
data = np.load(p / "transition-raw.npz")
preambles = json.loads((p / "transition-preambles.json").read_text())
pre = {c: np.array(v.split(","), dtype=float) for c, v in preambles.items()}
assert np.array_equal(pre["CHAN1"][4:7], pre["CHAN2"][4:7]), "Channel time grids differ"
assert len(data["CHAN1"]) == len(data["CHAN2"])
t = (np.arange(len(data["CHAN1"])) - pre["CHAN1"][6]) * pre["CHAN1"][4] + pre["CHAN1"][5]
volts = {c: (data[c].astype(float) - pre[c][8] - pre[c][9]) * pre[c][7] for c in pre}
total, lower = volts["CHAN1"], volts["CHAN2"]
valid_total = (data["CHAN1"] > 0) & (data["CHAN1"] < 255)
valid_lower = (data["CHAN2"] > 0) & (data["CHAN2"] < 255)
valid = valid_total & valid_lower
upper = np.where(valid, total - lower, np.nan)
baseline = (t < -0.03) & valid
summary = {
    "points": len(t), "sample_interval_s": float(pre["CHAN1"][4]),
    "time_start_s": float(t[0]), "time_end_s": float(t[-1]),
    "note": "Voltages are uncorrected scope readings. Saturated channels are excluded; math is invalid if either channel saturates.",
}
for name, values, mask in [("total", total, valid_total), ("lower", lower, valid_lower), ("upper", upper, valid)]:
    considered = mask & (t > -0.03)
    idx = np.flatnonzero(considered)
    peak = idx[np.nanargmax(values[idx])]
    summary[name] = dict(baseline_v=float(np.nanmedian(values[baseline])),
                         max_v=float(values[peak]), max_time_s=float(t[peak]),
                         min_valid_v=float(np.nanmin(values[idx])))
(p / "transition-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))

# Report broad excursions above 1.30 V using 20 us block means, avoiding
# fragmentation by switching ripple. Durations have about 20 us resolution.
width = 400
n = len(t) // width
bt = t[:n * width].reshape(n, width).mean(axis=1)
pulses = []
for name, values in [("lower", lower), ("upper", upper)]:
    by = values[:n * width].reshape(n, width).mean(axis=1)
    above = (by > 1.30) & (bt > -.03) & (bt < .1)
    edges = np.diff(np.r_[False, above, False].astype(int))
    for start, end in zip(np.flatnonzero(edges == 1), np.flatnonzero(edges == -1)):
        if (end - start) * 20e-6 < .0001:
            continue
        lo, hi = bt[start] - 10e-6, bt[end - 1] + 10e-6
        mask = (t >= lo) & (t < hi)
        pulses.append(dict(domain=name, start_ms=float(lo * 1000),
                           end_ms=float(hi * 1000), duration_ms=float((hi - lo) * 1000),
                           median_v=float(np.nanmedian(values[mask])),
                           peak_v=float(np.nanmax(values[mask]))))
pulses.sort(key=lambda row: row["start_ms"])
(p / "transition-pulses.json").write_text(json.dumps(pulses, indent=2) + "\n")

# Envelope reduction preserves short excursions when displaying many samples.
def envelope(axis, values, mask, color, label, bounds):
    idx = np.flatnonzero((t >= bounds[0]) & (t <= bounds[1]))
    stride = max(1, len(idx) // 10000)
    usable = len(idx) // stride * stride
    ix = idx[:usable].reshape(-1, stride)
    y = np.where(mask[ix], values[ix], np.nan)
    center_t = np.mean(t[ix], axis=1) * 1000
    good = np.any(np.isfinite(y), axis=1)
    ymin = np.full(len(y), np.nan); ymax = ymin.copy(); ymedian = ymin.copy()
    ymin[good] = np.nanmin(y[good], axis=1)
    ymax[good] = np.nanmax(y[good], axis=1)
    ymedian[good] = np.nanmedian(y[good], axis=1)
    axis.fill_between(center_t, ymin, ymax, color=color, alpha=.35)
    axis.plot(center_t, ymedian, color=color, lw=.8, label=label)

for name, bounds in [("transition-overview", (max(-.03, t[0]), min(.15, t[-1]))),
                     ("transition-detail", (-.003, min(.044, t[-1])))]:
    fig, axes = plt.subplots(3, 1, figsize=(11, 8), sharex=True, layout="constrained")
    for ax, label, values, mask, color in [
        (axes[0], "Total VDD — CH1", total, valid_total, "#b88400"),
        (axes[1], "Lower half-stack — CH2", lower, valid_lower, "#008ea1"),
        (axes[2], "Upper half-stack — CH1 − CH2", upper, valid, "#903bb0")]:
        envelope(ax, values, mask, color, label, bounds)
        ax.set_ylabel("Volts"); ax.grid(alpha=.2)
        legend_location = "upper right"
        if name == "transition-detail":
            legend_location = "upper left" if ax == axes[0] else ("upper center" if ax == axes[1] else "upper right")
        ax.legend(loc=legend_location)
        if ax != axes[0]:
            ax.axhline(1.20, color="#b43a32", ls="--", lw=1, label="1.20 V")
        ax.axvline(0, color="gray", ls=":", lw=.8)
        if name == "transition-detail":
            ax.axvline(40.32, color="black", ls="--", lw=.8)
            ax.set_ylim((2.20, 2.49) if ax == axes[0] else (.86, 1.64))
    if name == "transition-detail":
        for event, chip in zip(pulses, ["A1", "A3", "A2"]):
            ax = axes[1] if event["domain"] == "lower" else axes[2]
            x = (event["start_ms"] + event["end_ms"]) / 2
            ax.text(x, 1.62, f"{chip} update*\n{event['peak_v']:.3f} V peak\n{event['duration_ms']:.2f} ms >1.30 V",
                    va="top", ha="center", fontsize=9)
        axes[0].text(40.1, 2.48, "Power off", ha="right", va="top", fontsize=9)
    axes[-1].set_xlabel("Time relative to scope trigger (ms)\n*ASIC labels inferred from serial update order; last pulse ends at guard shutdown.")
    axes[-1].set_xlim(np.array(bounds) * 1000)
    fig.suptitle("bitaxeProto 1103 — guarded 400→425 MHz transition\nMeasured scope volts; clipped portions omitted", fontsize=14)
    fig.savefig(p / f"{name}.png", dpi=160)
    if name == "transition-detail":
        fig.savefig(p / f"{name}.pdf")
    plt.close(fig)
