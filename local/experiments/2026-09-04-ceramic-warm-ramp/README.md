# Ceramic output capacitors and revised compensation: warm-ramp rerun

## Outcome

The 4 × 100 µF ceramics plus revised compensation did **not materially reduce
the warm 300→325 MHz transition**. Warm 300 MHz qualified at 99.55%, but both
external half-stack screening limits were exceeded during the first higher
step. The host stopped the test and firmware confirmed ASIC power off. No
frequency above 325 MHz, ten-second 325 MHz qualification, or sustained pool
mining was tested. A1/A2 D2 remained healthy in the short post-launch sample.

## Hardware, firmware and report

The user reports replacing C4's 330 µF polymer with four 100 µF ceramics.
Their `main/power/vcore.c` edit changes COMPENSATION_CONFIG from
`13 11 8C 11 86` to `13 11 8C 19 86`. This edit was preserved. An existing
warmup run was stopped with serial X before building/flashing; it reported
`WARM END ... ASIC_power=OFF` at boot timestamp 130.182 s.

The guarded warm-ramp build and app-only flash succeeded using ESP-IDF 5.5.1.
Esptool verified the written data hash. New boot readbacks at 2.041–2.065 s
confirmed ON_OFF_CONFIG 0x1F, VOUT_SCALE_LOOP 0.25, switching 550 kHz, and
**COMPENSATION_CONFIG 13 11 8C 19 86**. Exact probe binary and relevant sources
are retained locally; firmware hashes are in `firmware-sha256.json`.

The new report reviewed was
`/Users/skot/Bitcoin/bitaxe/bitaxeProto-1103x/WebBench/WBDesign-1103x-ceramic.pdf`,
September 4, 2026, pages 2, 7, 8 (text extracted and rendered for inspection).
It models 4 × 100 µF GRM32EC80J107ME20L plus 2 × 47 µF, a 360 nH inductor,
550 kHz switching, 2.2 V/30 A, and predicts 6.147 mV p-p ripple and 6.171 kHz
crossover. The recommended loop gains and displayed PMBus component values
are separate report fields; the PDF does not print the raw compensation byte
sequence. Register verification here establishes that the user's requested
bytes were installed, not an independent reconstruction of all WEBENCH values.

Both capacitance and compensation changed in this comparison; their individual
effects are not isolated. The earlier run attached during warmup, whereas this
run followed a fresh firmware reset. The warm gate, voltage, launch sequence
and scope settings match. It is a single transition per configuration, not a
statistical repeatability study.

## Protocol and instrumentation

Temporary 1140 mV per half; initial 100–300 MHz qualification, warm 300 MHz
until hottest >=54.5°C and coldest >=52°C for ten seconds, then warm baseline
qualification. Fan lease uses 10–20% through most warmup and returns to normal
control on expiry or exit. NVS settings are not changed.

CH1 is directly at the TPS output capacitors with **50 Ω termination**; CH2
is GND0−GND with 1 MΩ termination. Both are DC coupled, 1× and bandwidth
limited to 20 MHz. CH1=FIFT is asserted before each arm, before and after every
capture, and at cleanup. CH1−CH2 combines physical measurement locations and
is an upper-half estimate that may include distribution/ground differences.

700,000 samples/channel at 2 MSa/s cover −75 to +275 ms around the forced
trigger. The 195 ms launch fits within that record with the required margin.
No samples clipped. The host checks raw half-stack peaks <1.25 V and **every
sliding 20 µs mean <1.22 V**, unchanged from the earlier comparison. Firmware's
independent 20 ms sampled PCB limit stays 1.20 V. Scope rejection happens after
the event while downloading; it is not a hardware transient clamp.

The 8 mV CH1 sample increment and this transient capture setup do not establish
whether switching ripple meets the report's 6.147 mV prediction. Pre-event
sample spreads in `capture-metrics.json` are not a dedicated ripple measurement.

## Measurements

Temperature gate completed at 304.537 s. Warm 300 MHz qualified at
1490.85/1497.60 GH/s (99.55%) with PCB rails 1133/1147 mV. Baseline scope
capture passed: lower/upper raw maxima 1.196/1.192 V, maximum sliding means
1.1535/1.1539 V.

325 MHz trial began at 318.644 s and LAUNCHED was logged at 318.839 s.
The first post-launch temperatures A1/A2/A3/A4 were 54.18/53.36/53.36/56.00°C.

| Metric | Previous polymer | Ceramics + new compensation |
|---|---:|---:|
| TPS pre-event baseline | 2.29450 V | 2.29927 V |
| TPS raw minimum / maximum | 2.152 / 2.416 V | 2.160 / 2.416 V |
| TPS sliding 20 µs min / max | 2.17920 / 2.39740 V | 2.17520 / 2.40900 V |
| TPS mean overshoot from baseline | +102.90 mV | +109.73 mV |
| TPS mean undershoot from baseline | −115.30 mV | −124.07 mV |
| Lower raw peak | 1.420 V | 1.424 V |
| Lower sliding 20 µs peak | 1.4038 V | 1.4071 V |
| Upper estimate raw peak | 1.3240 V | 1.3320 V |
| Upper estimate sliding 20 µs peak | 1.2684 V | 1.2804 V |

The aligned overlay is `ceramic-comparison.png` / `.pdf`. Captures are aligned
on the CH2 averaged peak, not a measured on-wire PLL edge. Main-rail waveforms
are very similar; the midpoint decay differs somewhat but neither peak is
materially reduced. At the ceramic lower-half averaged peak, simultaneous
TPS/lower/upper-estimate voltages are 2.4058/1.4071/0.9987 V. Both total supply
movement and midpoint redistribution remain. This result weakens the hypothesis
that changing C4's ESR alone would solve this event; it does not isolate the
root cause or prove the TPS control loop is correct.

The host sent X after 2.505 s of downloading; firmware powered off at
322.656 s. The sampled guard saw maxima only 1136/1150 mV and missed the fast
scope event. No further PLL step was issued.

Four post-launch snapshots yield three complete counter intervals, about
2.93 seconds per ASIC. D2 throughput A1/A2/A3/A4 was **100.86/99.17/99.23/98.19%**
of ideal. All 16 domains had zero sampled zero-PASS cores. This is a short
observation of passing hash activity, not a clock-delivery measurement or proof
of sustained 325 MHz operation; a sub-second disturbance before the first
snapshot is not excluded. Repeated five-second I2C NACK messages also occur
in the previous run; they did not cause the stop, and this experiment does not
diagnose their source.

## Final state and next comparison

ASIC power off; API temp/temp2=0, hashRate=0. Fan returned to 70%, 2727 RPM.
NVS remains 400 MHz / 1160 mV, automatic fan, min 99%, target 60°C.
API actualFrequency=325 is retained state, not evidence of active hashing.
Scope is STOP, **CH1 remains FIFT (50 Ω)**; CH2 remains OMEG, both DC/1×.
See `final-api-state.json`, `final-scope-state.json`, and per-capture settings.

The guarded ceramic-compensation image remains installed and reruns after a
reset. Source warm-probe flag was restored to zero after flashing, and a normal
image with the same revised compensation was built successfully and saved as
`normal.bin`; it was not flashed. Persistent mining configuration was not
changed. No commits or unrelated edits were made.

A useful next sequencing comparison is to separate an identical-frequency
work/configuration reset from an actual PLL change while recording the same
rails. The present compound launch does not identify which command causes
which load edge. This proposed follow-up was not run here.
