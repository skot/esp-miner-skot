# Warm 1103x frequency-ramp experiment

The user requested a balanced operating point near 300 MHz, reduced fan
speed, warming toward 55°C, then further frequency increases to see whether
balance improves. Voltage stays at 1160 mV per PCB half-stack / 2.32 V total.
The saved 400 MHz frequency and all NVS settings are preserved.

## Result: warm baseline passed; no higher-frequency update

The initial cold 300 MHz qualification passed at 99.09% (1484.05 GH/s).
The first temperature snapshot at that frequency was A1 35.54, A2 34.55,
A3 35.21, A4 38.18°C. After approximately four minutes of warming,
the warm 300 MHz qualification passed at 99.55% (1490.80 GH/s), with
1155/1164 mV PCB rails and 11.38 A regulator current. Final temperatures
were approximately 53.4/52.4/52.9/56.3°C. These are qualification workloads,
not a pool-side hashrate measurement or a long stability soak.

All 16 domains averaged 99.12–99.79% ideal passing throughput across the
complete warmup intervals. A1/A2 D2 averaged 99.38/99.30%; their PVT readings
changed gradually from approximately 288.5 to 287 mV. No D2 dropout or abrupt
recovery appeared. This run started healthy at 300 MHz, so it does not show
warming recovering a previously failed D2 state.

The host captured the warm 300 MHz baseline before sending G for 325 MHz.
It rejected raw scope peaks of **1.224 V lower / 1.216 V upper** and sent X;
ASIC power was off at boot timestamp 327.401 s. No higher-frequency PLL
update occurred. The independent sampled guard saw maxima 1167/1166 mV.
The fan lease ended and ordinary startup fan control resumed at 70%.
NVS remains 400 MHz / 1160 mV, auto fan, 99% minimum and 100% manual.

Scope means were 1.1742/1.1784 V, about 19/14 mV above the contemporary PCB
telemetry. Every threshold crossing was isolated to one recorded sample at
2 MSa/s; that does not bound the actual analog pulse width at 20 MHz bandwidth.
The maximum 20 µs block means were 1.1776/1.1822 V. This is not the earlier
millisecond-scale transient plateau, but neither the offset nor digital
averaging establishes that these peaks are measurement artifacts. The raw
1.20 V criterion was kept; no calibration or post-processing exception was
used to continue. `warm-baseline300/statistics.json` preserves these values.

A separate 1140 mV warm-ramp comparison follows in
`../2026-09-04-warm-ramp-1140/`, keeping the same limit and protocol. Changing
voltage makes that a separate condition, not a repeated 1160 mV trial.

## Fan control

The initial API read reported automatic fan control, minimum 99%, manual
100%, and target 60°C. With the ASICs off, the actual startup command was
70%, approximately 2729 RPM. `fan-original-settings.json` preserves those
values. A temporary controller lease overrides startup/minimum settings;
the normal overheat branch still has priority. The lease must be refreshed
within five seconds, otherwise ordinary fan control resumes.

The ramp starts at 20%. During warmup, the command uses the hottest ASIC:
below 50°C it is 20% for the first minute, then 10%; 50–54°C uses 15%,
54–56°C uses 20%, 56–58°C uses 30%, 58–60°C uses 50%, and >=60°C uses 100%.
The fan is never deliberately commanded below 10%. Fresh sensor snapshots
refresh the lease; invalid or >=70°C readings stop the ASIC experiment.
Ending the experiment disables the override and restores ordinary fan control.
The lease's clamping, expiry, disable and 32-bit tick rollover were checked
with a host C test of the actual setter implementation.

## Protocol and limits

1. Cold startup: normal broadcast 25 MHz steps from 100 through 300 MHz,
   with normal qualification at each step. The independent 20 ms sampled
   guard powers off on invalid voltage telemetry or either PCB half >=1.20 V.
2. Hold 300 MHz and log per-domain PASS/FAIL, D2 PVT, all ASIC temperatures,
   PCB rails, TPS current, fan PWM and RPM. Wait for hottest >=54.5°C,
   coldest >=52°C and hottest <=60°C continuously for ten seconds. The warmup
   limit is ten minutes. Counter windows are explicitly rearmed every
   100 seconds to prevent expiration/wrap; this reapplies SPD/FIFO actions
   and means the entire warmup is not one uninterrupted passive window.
3. At the warm temperature, qualify the existing 300 MHz profile without
   changing PLL dividers. This counter rearm does apply SPD/FIFO actions.
4. Capture the warm 300 MHz baseline on the Rigol before any higher PLL step.
   If it exceeds the scope limit, stop before advancing frequency.
5. For each 25 MHz step, up to a maximum of 600 MHz, use a USB handshake:
   G requests a rendezvous; the MCU acknowledges ARMED after its slow sensor
   read; the host force-triggers the scope and sends T; the MCU polls T at
   10 ms intervals, then performs the broadcast PLL/work launch and one
   180-second counter arm. This keeps the launch inside the scope record.
6. The host downloads paired waveform chunks and sends X immediately on a
   measured peak >=1.20 V or clipping. A complete capture that covers the
   launch and passes both half-stack checks receives R. MCU guard trips or
   missing/rejected handshakes end the test with ASIC power off.
7. After R, observe for ten seconds. Every domain must reach >=97% of ideal
   passing throughput, >=97% pass fraction, and have no zero-PASS core in
   the accumulated window. If a step is weak but its voltage capture passed,
   hold fixed PLL/work for 120 seconds and assess the final roughly ten-second
   interval for recovery. No subsequent step is attempted unless it passes.

Only the higher-frequency steps and warm baseline get synchronized scope
captures. Cold startup uses the sampled guard, which cannot bound fast
excursions. The scope uses CH1=VDD−GND and CH2=GND0−GND, with upper=CH1−CH2.
Both channels are DC coupled, 1×, 20 MHz bandwidth. Each force-triggered
record has 700,000 points/channel at 2 MSa/s, approximately −75 to +275 ms.
The 2.0 V edge level prevents unrelated auto-triggers; force triggering is
explicit. No scope offset correction is applied. Download/audit is software
protection after the event, not hardware overvoltage shutdown.

Sensors and counters are read sequentially. Counter arming and temperature/
PVT conversion writes are not proof that ASIC clocks were gated. PASS+FAIL
measures hash-check activity, not clock delivery directly. `wait_scope`
counter intervals are excluded from throughput summaries and curves because
the first such wait follows a completed, frozen qualification counter window.

## Artifacts

`capture_warm.py` supervises the USB/scope handshakes; `analyze_warm.py`
checks counter continuity and interval-rate arithmetic, exports CSV/JSON,
and renders the temperature/voltage/throughput/fan timeline. The throughput
curve is a five-sample weighted average that does not cross counter epochs
or phase boundaries. Individual interval data remain in the CSV/JSON.
Each scope capture has its own compressed raw bytes, exact preambles, audit
and plotted trace. Full serial/build logs and exact image/source snapshots
are retained locally but ignored; condensed evidence and hashes are included.
