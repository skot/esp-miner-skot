# Startup regression factor comparison — 2026-09-06

## Finding

**The recent repeatable five-domain / approximately 2.05 TH/s regression follows
cooler thermal conditions during ASIC reinitialization, unloaded PLL ramp and
first work.** Lowering warmup to 40 C produced this failure with either first-work
sequence. Both sequences reached approximately 2.965 TH/s with the original hot
warmup. A hot-then-cooled control also failed, so having completed a hot warmup
previously is insufficient if the ASICs cool before reinitialization.

The test does not yet separate the temperature-sensitive phase within power-on,
reset, PLL ramp and first job, identify the internal electrical mechanism, or
establish the minimum reliable temperature. It explains the severe recent
regression without claiming to resolve the earlier intermittent hot-start losses.

## Controlled comparisons

| Thermal condition | First-work sequence | Temperatures before first work (A1/A2/A3/A4 C) | First 10s GH/s | Whole valid window GH/s | Outcome |
|---|---|---|---:|---:|---|
| Original hot warmup, exact saved image | Original | 56.98 / 55.50 / 56.49 / 56.00 | 2961.51 | 2964.49 | All domains active |
| Original hot warmup | Current passive | 57.15 / 55.83 / 56.98 / 56.65 | 2961.88 | 2964.54* | All domains active |
| 40 C warmup | Original | 46.43 / 44.45 / 45.77 / 45.77 | 2044.86 | 2046.61 | Five domains missing |
| 40 C warmup | Current passive | 46.76 / 44.94 / 46.26 / 46.43 | 2046.04 | 2046.67 | Same five domains missing |
| Hot warmup, then cool before reset | Original | 46.10 / 44.12 / 45.44 / 45.44 | 2045.00 | 2046.75 | Same five domains missing |

Missing domains: **A1 D0/D2, A2 D0/D2, A3 D2**. They contributed less than
0.0001% of their ideal throughput in the full windows. In the cold/passive trial,
A2 D2 registered one isolated PASS; in the hot-then-cooled trial A1 D2 registered
one isolated PASS. These events were not sustained recovery. Successful hot runs
retain the previously observed single zero-PASS A1 D2 core; all 16 domains hash.

All three cooler launches stayed deficient throughout approximately 120 seconds.
Their final ten-second totals were 2046.71 / 2045.69 / 2046.15 GH/s respectively,
with zero interval PASS in all five affected domains. Subsequent heating to
approximately 58–60 C did not restore them. Every run's peak was 60.12 C and
there were no invalid thermal samples. The successful hot launches already
passed their first ten-second windows; they did not depend on late recovery.

One new trial was run per comparison condition. The exact saved original image
has succeeded twice across September 5–6. These results are strong evidence for
the thermal trigger of this particular failure pattern, not a statistical startup
reliability estimate or proof that launch ordering can never matter.

## Thermal-history control

The final trial first qualified the original hot 300 MHz warmup at
55.17 / 54.51 / 54.68 / 55.01 C. It then kept the same 300 MHz PLLs and work,
ran the fan at 100%, and read temperatures until the hottest ASIC was <=42.5 C.
After 128.057 seconds of cooling it reached 42.47 / 41.64 / 41.81 / 41.97 C.
The usual full supply cycle, reinitialization, voltage ramp and unloaded PLL ramp
then followed. Pre-job temperatures were only about 0.33 C below the earlier
40 C/original-sequence trial. The same failure resulted.

This separates prior hot warmup history from remaining warm through the startup
sequence. It does **not** isolate temperature at the instant of job submission
from temperature during the preceding reinitialization and PLL changes.

Cooling added no work, PLL or SPDLOG writes. It used existing temperature reads
and a temporary fan lease, preserved temperature/communications/manual-stop
checks, and had a 180-second timeout that would abort rather than launch outside
the target band. The four ASICs underwent the normal full supply reset afterward.

## What the four proposed factors mean now

1. **Warmup:** changing its threshold to 40 C is a demonstrated trigger for the
   severe recent failure in the archived harness. However, completing a 55 C
   warmup is not itself sufficient after cooling.
2. **Temperature before first work:** the strongest measured predictor, tracking
   thermal conditions through reset/ramp/launch. Hot launches at 55.5–57.2 C
   worked; cooler launches at 44.1–46.8 C failed. Exact sensitive phase and minimum
   threshold remain unisolated.
3. **First-work sequence:** both original post-job strobe and current pre-job arm
   worked hot and failed cool. No sequence interaction is required to explain
   this five-domain regression; rarer interactions are not excluded.
4. **Failure observation:** extending to 120 seconds did not rescue any of the
   three cooler launches, even after they warmed near 60 C. The ten-second guard
   identifies a real persistent deficiency in these runs, rather than merely
   missing the successful image's normal recovery period.

Next production refinement supported by this evidence: restore the original hot
warmup and check fresh temperatures before releasing first work. Keep the current
first-work sequence initially, then test hot-start repeatability separately.
No production source was changed during this investigation.

## Firmware, validation and measurement details

The base was reconstructed in isolated detached worktree
`/tmp/bitaxe-1103-regression`, at c91844e with archived ASIC initialization,
MC3, TPS, Vcore and board-config source snapshots restored. Existing production
working-tree changes remain untouched. Same stock 1103 board, MAC
28:84:85:BA:96:08, USB /dev/cu.usbmodem11434201, IP 192.168.30.9.

All comparisons preserve 1160 mV/half, stock 550 kHz TPS configuration including
the archived off-state scale-seed test, full ASIC supply cycle, original 25 MHz
PLL steps and settling, and the archived observation/fan policy. The passive
option copies current register ordering and checked work-write helpers without
dashboard instrumentation; original helpers remain intact. Failed work writes
propagate. The only timer adjustment is noted below.

App-only flashing preserves NVS, partitions and web assets. Fan is temporarily
manual 100% with ASIC power off between tests, with at least 120 seconds between
subsequent trials and several minutes before the first. Original fan settings
are restored before each startup and at completion. No scope was connected.
Bench voltage/throughput limits remain report-only; thermal, communications,
hardware-fault and manual-stop checks remain. Independent sampled half-stack
maxima were 1194/1176, 1174/1203, 1191/1203 and 1181/1203 mV for the four new
runs respectively. These are separate maxima with no scope-verified peak/duration.
They do not establish that the supply is free of transients.

*Measurement correction:* hot-passive-01 initially retained production timer
count 1431 (120.04 seconds). The approximately 121-second observation outlasted
that epoch, causing an artificial tail-rate decline. `window-comparison.json`
excludes the final two snapshots per chip near/past expiration, using samples
ending within 119 seconds of first work. Raw logs and uncorrected `result.json`
are preserved. Later passive runs use count 2146 (180.02 seconds), matching the
original bench, with no exclusion needed. Counter values are never reset during
the hold. Early/late qualification is calculated from interval rates using
production thresholds; archived snapshot cadence differs from production.

Host tests passed for passive register order/error propagation, the original
power-cycle/ramp sequence, and bounded cooling/control error handling. ESP-IDF
5.5.1 builds passed for every flashed variant. The isolated build reused prebuilt
web assets; only application binaries were flashed and verified. Final diff
whitespace check passed.

Final state: the **hot-then-cooled experimental bench remains installed**, ASICs
automatically powered off, rail readback 36 mV. Original fan auto/manual settings
were restored; saved 600 MHz / 1160 mV settings remain. This is synthetic work,
not pool mining, and this archived application lacks the new dashboard endpoint.

Artifacts: `matrix.json`; per-run binary/source hashes, full serial logs,
`window-comparison.json`, thermal/rail results and final state. Reproduction helpers:
`prepare_variant.py`, `run_trial.py`, `analyze_trial.py`, `summarize_matrix.py`,
`cool_after_hot.inc`, and host tests. Raw logs/binaries/snapshots remain local.
