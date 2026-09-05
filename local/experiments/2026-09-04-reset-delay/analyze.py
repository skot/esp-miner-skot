"""Extract delayed-reset experiments from the saved serial log (stdlib only)."""
import csv
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
rows = {}
timings = {}
for line in path.read_text(errors="replace").splitlines():
    timing = re.search(r"RESET_DELAY TIMING repeat=(\d+) delay_ms=(\d+) pll_ms=([\d.]+) reset_call_gap_ms=([\d.]+) reset_count=(\d+)", line)
    if timing:
        r, d, pll, gap, resets = timing.groups()
        timings[(int(r), int(d))] = dict(pll_ms=float(pll), reset_call_gap_ms=float(gap), reset_count=int(resets))
    stage = re.search(r"RESET_DELAY-r(\d+)-d(\d+)-w(\d+)", line)
    if not stage:
        continue
    repeat, delay, window = map(int, stage.groups())
    row = rows.setdefault((repeat, delay, window), dict(repeat=repeat, delay_ms=delay, window=window))
    if "RESET_DELAY DOMAIN" in line:
        m = re.search(r"A(\d) D(\d) absolute=([\d.]+)% zero=(\d+)", line)
        chip, domain, rate, zero = m.groups()
        row[f"A{chip}_D{domain}_percent"] = float(rate)
        row[f"A{chip}_D{domain}_zero"] = int(zero)
    if "averaged taps/domains mV:" in line:
        m = re.search(r"A(\d).*\| D0=([\d.]+) D1=([\d.]+) D2=([\d.]+) D3=([\d.]+)", line)
        if m:
            chip, *volts = m.groups()
            row.update({f"A{chip}_D{d}_mv": float(v) for d, v in enumerate(volts)})
    if "domains: lower=" in line:
        m = re.search(r"lower=(\d+) upper=(\d+).*TPS current=([\d.]+)", line)
        row.update(zip(("lower_mv", "upper_mv", "current_a"), map(float, m.groups())))
    if "chip" in line and "throughput=" in line:
        m = re.search(r"chip(\d)@.*throughput=([\d.]+)%", line)
        if m:
            row[f"A{int(m[1])+1}_percent"] = float(m[2])
    if "RESET_DELAY PROFILE" in line:
        row["accepted"] = line.split("accepted=")[-1].strip()

for row in rows.values():
    row.update(timings.get((row["repeat"], row["delay_ms"]), {}))
data = list(rows.values())
path.with_suffix(".json").write_text(json.dumps(data, indent=2) + "\n")
fields = list(dict.fromkeys(key for row in data for key in row))
with path.with_suffix(".csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(data)
for row in data:
    print("r{repeat} delay={delay_ms:4} w{window} {accepted:6} gap={reset_call_gap_ms:8.3f}ms A1D2={A1_D2_percent:6.2f}% A2D2={A2_D2_percent:6.2f}% PCB={lower_mv:.0f}/{upper_mv:.0f}".format_map({**dict(accepted="pending", reset_call_gap_ms=0, A1_D2_percent=0, A2_D2_percent=0, lower_mv=0, upper_mv=0), **row}))
