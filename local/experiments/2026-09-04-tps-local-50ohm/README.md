# Warm ramp with CH1 directly across TPS546 output capacitors

The user moved CH1 directly across the TPS546 output capacitors using 50 Ω
coax and explicitly required CH1 to remain terminated in 50 Ω throughout.
Initial readback confirmed CH1 FIFT (50 Ω), DC coupling, 1×; CH2 OMEG
(1 MΩ), DC, 1×. CH2 remains at GND0–GND per the established setup.

The device was already running the guarded warmup firmware at 300 MHz when
this task attached: an initial serial read at boot timestamp ~101 seconds
showed approximately 38–41°C and 1136/1144 mV PCB rails. No reset or flash
was issued. Consequently this run's saved log begins partway through warmup,
and does not contain the cold frequency qualification or the initial WARM
BEGIN line. The first observed counter value has no known prior sample;
analysis retains its logged dt separately and excludes that first interval.

## Scope and protocol

`capture_warm.py` explicitly sets CH1 to FIFT before acquisition setup, checks
termination, coupling and probe factor before every arm, and records them
before and after each capture. Its cleanup leaves CH1 in FIFT and saves final
readback. It never requests a different CH1 termination. CH2 termination is
checked without changing it. Both bandwidth limits stay at 20 MHz.

Capture timing, 2 MSa/s and 700,000 points/channel, firmware, voltage and
acceptance criteria match `../2026-09-04-warm-ramp-tolerant/`: warm at 300 MHz
until approximately 52–56°C; verify baseline; broadcast 25 MHz steps up to
600 MHz subject to per-step checks. The host stops at raw half-stack/derived
peaks >=1.25 V or a sliding 20 µs mean >=1.22 V. The independent firmware
sampled PCB limit remains 1.20 V. Scope checking is after-event software
protection, not a hardware transient limiter.

CH1 now measures regulator-local output. CH1−CH2 combines measurements at
different physical locations; distribution/ground voltage can therefore be
included. It is retained for conservative screening and labelled as an
estimate, not an exact direct measurement across the upper ASIC half-stack.
Comparison with the prior capture changes both probe location and the known
termination/setup; the prior run did not log termination. A difference alone
cannot isolate converter response from probing/distribution effects.

The loaded firmware uses a temporary 1140 mV per-half target. NVS remains
400 MHz / 1160 mV. The saved probe/source snapshots are the prior installed
image for reference; this task does not establish identity by reading flash.
Firmware settings and source were not changed during this rerun.

## Results

The warm temperature gate completed at boot timestamp 390.870 s. Warm
300 MHz qualified at 99.54% (1490.78 GH/s), with 1136/1144 mV PCB rails.
The baseline scope capture passed. CH1 termination read FIFT before and after
both baseline and transition captures; cleanup again set/read FIFT.

The broadcast 300→325 MHz trial began at 404.975 s and completed its launch
at 405.170 s (195 ms). Temperatures in the first post-launch snapshot were
53.52/52.53/52.86/56.49°C. The raw capture contains the transition and no
clipped samples. Directly across the TPS output capacitors, CH1 measured:

| Metric | Prior CH1 setup | TPS caps, 50 Ω |
|---|---:|---:|
| Pre-event baseline | 2.30967 V | 2.29450 V |
| Raw maximum | 2.448 V | 2.416 V |
| Raw minimum | 2.160 V | 2.152 V |
| Highest sliding 20 µs mean | 2.41200 V | 2.39740 V |
| Lowest sliding 20 µs mean | 2.19420 V | 2.17920 V |
| Mean overshoot from own baseline | +102.33 mV | +102.90 mV |
| Mean undershoot from own baseline | −115.47 mV | −115.30 mV |

The excursion relative to each baseline is nearly identical. This confirms
that the output-capacitor node itself moves during the transition; the event
cannot be explained solely by the prior probe's remote placement. It does
not isolate a defective regulator or prove the compensation is wrong: load
waveform, output filter, sensing and measurement setup still need review.
The comparison changes probe setup/location and repeats rather than captures
both locations simultaneously. `compare_tps_captures.py` retains the metrics
and aligned overlay; plots align on the lower-half averaged peak without
claiming this identifies an on-wire command edge.

The lower half peaked at 1.420 V raw / 1.4038 V sliding mean. The CH1−CH2
estimate peaked at 1.3240 / 1.2684 V. Both exceeded both existing criteria,
so the host sent X after 2.338 s of waveform downloading. Firmware powered
off at 408.985 s. Its sampled guard did not trip and saw only 1158/1148 mV
maxima. No step beyond 325 MHz or ten-second step qualification occurred.

Four post-launch snapshots (three full intervals, approximately 2.94 s)
showed all domains with no zero-PASS cores. D2 throughput A1/A2/A3/A4 was
100.91/99.12/99.21/98.26% of ideal. A1/A2 D2 PVT remained approximately
281.6–282.0 mV; settled PCB rails remained 1136–1137/1143–1144 mV. This
short healthy observation does not establish sustained mining or exclude a
sub-second disturbance before the first snapshot.

## Final state and verification

ASIC power is off. The fan lease ended and startup fan resumed at 70%,
2757 RPM. NVS remains 400 MHz / 1160 mV, auto fan, minimum 99%, manual 100%,
target 60°C. Retained API actualFrequency=325 does not mean active hashing.
The guarded warmup image remains installed and will rerun after a reset.

**CH1 is left in 50 Ω mode**, DC coupled and 1×. CH2 remains 1 MΩ/DC/1×.
See initial `scope-settings.json`, each capture's `input-settings.json`, and
`final-input-settings.json`. The scope is stopped. No TPS configuration,
firmware source, flash image or persistent miner setting was changed here.

Python compilation, counter continuity/rate checks and whitespace checks
passed. The analysis correctly excludes the first incomplete interval after
attaching to the live warmup. Scope and comparison plots were inspected.
Raw waveform bytes, exact preambles, audits, condensed evidence and hashes
are retained; full serial/capture logs remain local and ignored.
