# Warm 1103x ramp with a scope measurement allowance

The user explicitly requested proceeding above warm 300 MHz, allowing a
modest increase in the scope shutdown threshold for offset, spikes and noise.
This run reuses the previously installed guarded 1140 mV warm-ramp firmware.
Only the host scope acceptance criteria change. An ESP reset restarts the
experiment; no new firmware flash or NVS change is needed.

## Acceptance criteria

- Raw scope half-stack peak must remain below 1.25 V.
- Every sliding 20 µs mean must remain below 1.22 V.
- Clipping, incomplete launch coverage or missing MCU handshakes reject.
- Firmware's independent sampled 1.20 V PCB guard is unchanged, as are
  invalid-sensor and 70°C temperature stops and the five-second fan lease.

These are experimental measurement allowances, not a calibration correction
or a change to the ASIC voltage rating. Both raw channels are retained without
subtracting an assumed offset. Scope download/audit happens after the event
and cannot protect against the first fast excursion. The firmware guard
samples every 20 ms and also cannot bound fast transients.

`scope_limits.py` implements the raw and sliding-mean limits. Chunk checks
retain the previous 39 samples at 2 MSa/s so the 20 µs window spans download
boundaries. The complete capture is checked again before R is sent. Historical
baseline captures at 1160/1140 mV pass the revised criteria; the earlier
1140 mV broadcast transition with 1.504/1.348 V peaks still rejects. These
checks plus synthetic spike, plateau and invalid-data cases are recorded in
`test_scope_limits.py` and `criteria-validation.json`.

## Protocol

The MCU warms at 300 MHz with reduced fan speed until the hottest ASIC is
at least 54.5°C and the coldest at least 52°C for ten seconds. It qualifies
warm 300 MHz, then waits for the host's scope rendezvous. Each subsequent
25 MHz step broadcasts the PLL change and reapplies the qualification work,
then arms counters once. The maximum requested frequency is 600 MHz.

After an accepted scope capture, each step observes ten seconds and checks
all domains for at least 97% ideal passing throughput, at least 97% pass
fraction, and no zero-PASS cores in the accumulated counter window. A weak
step with acceptable voltage gets a 120-second fixed-PLL/work recovery hold;
the last roughly ten-second interval must pass before advancing. The test
powers off on completion or rejection. Full MCU sequencing and fan details
are in `../2026-09-04-warm-ramp/README.md`.

Warmup counter windows are explicitly rearmed every 100 seconds; this applies
SPD/FIFO strobes. The warmup is not a completely passive thermal experiment.
Temperature and PVT reads also write conversion controls. PASS+FAIL measures
hash-check activity, not delivery of the clock itself. Qualification throughput
is not pool-side hashrate or a sustained mining soak.

`analyze_warm.py` exports counter data, temperature/PVT/fan plots and waveform
statistics. Its historical red 1.20 V line is retained as a reference; each
capture's `audit.json` records the actual acceptance limits and moving means.

## Result: warm 325 MHz launched, healthy short hashing observation, scope rejection

Warm 300 MHz qualified at 99.57% (1491.09/1497.60 GH/s), with PCB rails
1138/1142 mV. All ASICs were approximately 52–56°C. The warm baseline scope
capture passed the revised criteria: peaks 1.200/1.196 V and maximum sliding
20 µs means 1.1569/1.1608 V.

The host then requested the **300→325 MHz broadcast launch**. MCU timestamps:
ARMED 358.453 s, TRIAL 358.464 s, all PLL0s locked at 325 MHz 358.502 s,
LAUNCHED 358.662 s. The 198 ms launch fit within the +275 ms scope record
using the host's 50 ms timing allowance; this does not locate individual
register writes on the waveform.

The scope measured **1.428 V lower / 1.328 V upper** raw peaks and maximum
sliding 20 µs means **1.4133 / 1.2755 V**. Both halves exceeded both revised
criteria, without clipping. The event cannot be dismissed as the earlier
small baseline peaks. Scope volts remain uncorrected. The total rail peaked
at 2.448 V; the transient is not a proof that the total supply was perfectly
steady. The waveform shows opposite-direction half-stack movement.

Using nonoverlapping 20 µs block means for duration estimates, the lower
half exceeded 1.20 V for 220 µs, 1.22 V for 160 µs and 1.30 V for 60 µs.
The upper exceeded 1.20 V for 40 µs. These are block-based estimates, not
analog edge measurements. `step-325/durations.json` retains all thresholds;
`transition-zoom.png/pdf` shows the event. The criteria use sliding means,
which differ slightly from these diagnostic block means.

**D2 did not show a persistent failure in the available observation.** There
were three snapshots and two complete intervals per ASIC, approximately
1.94 seconds. A1/A2 D2 passing throughput was 100.73/99.33% of ideal, with
FAIL fractions 0.41/0.45%. A3/A4 D2 was 100.05/98.46%. All domains had zero
zero-PASS cores in these snapshots. Values slightly above 100% reflect short
hash-count statistics and sequential sampling, not extra clock frequency.
A1/A2 D2 PVT remained about 282 mV, A3/A4 about 286.6 mV; PCB telemetry
returned to 1137–1138 / 1142–1143 mV. This is a short observation, not the
planned ten-second qualification or a sustained 325 MHz mining run.

The host sent X after 2.279 seconds of waveform downloading. Firmware
powered ASICs off at 361.494 s. The sampled guard did not trip and recorded
maxima only 1138/1145 mV, missing the fast event. No scope acceptance R,
120-second recovery hold, or step above 325 MHz occurred.

This establishes that the tested warm launch can be followed by healthy
hashing despite a large electrical transient. It does not demonstrate that
warmth improved the transition relative to a matched cold 300→325 launch,
or establish behavior at 400–600 MHz. The user-authorized modest measurement
allowance worked for the baseline but was insufficient for this transient.
No further threshold increase was made during the run.

## Final state and validation

ASIC power is off; the previously installed guarded 1140 mV warm-ramp image
remains installed and reruns on reboot. The source warm-probe flag remains
zero. Firmware was not edited or rebuilt this turn. Saved NVS remains
400 MHz / 1160 mV, auto fan, minimum 99%, manual 100%, target 60°C. The fan
lease expired/ended and startup fan control resumed at 70%, 2699 RPM.
`actualFrequency=325` in the API is retained software state after power-off.

Historical-capture and synthetic limit tests passed. Counter continuity and
rate arithmetic assertions passed; Python compilation and whitespace checks
passed. Full and zoomed waveform plots were inspected. Full logs and exact
firmware/source snapshots remain local and ignored; condensed evidence,
hashes, waveform bytes/preambles, audits, and per-domain CSV/JSON are retained.

## Follow-up: TPS546 output response is also implicated

The user noted that CH1 shows the event on main VDD. That is correct: earlier
language treating the TPS output as steady was too strong for these fast
captures. The scope sees the total supply move as well as the midpoint.

`analyze_rail_decomposition.py` compares simultaneous 20 µs moving averages
against the mean of the pre-event 0–8 ms interval. Baseline total/lower/upper
was 2.3097/1.1535/1.1562 V. At the lower-half averaged peak, the same window
measured total/lower/upper **2.4102/1.4133/0.9969 V**. Changes were therefore
+100.5/+259.8/−159.3 mV. At the upper-half averaged peak, they measured
**2.2176/0.9421/1.2755 V**: changes −92.1/−211.4/+119.3 mV. These are
simultaneous window values, not independently selected peaks.

An equal split of the first total voltage would be approximately 1.2051 V
per half, whereas the measured split was 1.4133/0.9969 V. Both total-output
response and substantial imbalance are present. Algebraically, the lower
rise decomposes into half the total rise (+50.3 mV) plus midpoint movement
relative to half the total (+209.5 mV). This decomposition does not identify
which physical mechanism caused the other or quantify a causal percentage.

A load-release overshoot followed by a load-application undershoot is a
plausible interpretation. Neither fast load current nor exact register-write
timing was measured, so it remains an interpretation. Probe placement matters:
VDD measured at ASIC test points can also include distribution/ground-path
impedance, and CH1 is not automatically a measurement directly across the
regulator's local output capacitors or remote-sense nodes.

The current 1103 source configures 550 kHz, VOUT_SCALE_LOOP=0.25 and
COMPENSATION_CONFIG bytes 13 11 8C 11 86. Previous notes report register
readback verification and WEBENCH-derived compensation. Those checks do not
validate the response to the actual ASIC load step and installed capacitance.
No compensation, voltage or frequency settings were changed in this review.

TI's TPS546D24S datasheet sections 8.2.2.4.1 and 8.2.2.4.2 discuss load-step
capacitance, loop bandwidth and capacitor impedance; regulator transient
response depends on those quantities. Section 7.6.24 describes commanded
voltage slew through VOUT_TRANSITION_RATE, which is a different control from
load-transient compensation. Source: https://www.ti.com/lit/ds/symlink/tps546d24s.pdf

Next diagnostic: establish the CH1 probe location and compare a capture
across the regulator output capacitors with VDD at the ASICs; include fast
load-current or switch-node information if available. Then review actual
output L/C/ESR, sense routing and compensation against the measured load step.
Reducing total-rail movement might also reduce the imbalance, but that has
not yet been demonstrated. The board remains powered off.
