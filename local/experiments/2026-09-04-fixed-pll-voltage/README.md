# Fixed-PLL voltage control

The first attempt aimed to reach healthy 400 MHz / 1160 mV, then lower to
1140 mV without changing PLLs or work. The independent guard stopped its
startup during 325→350 MHz at PCB 1219/1100 mV, before the counter baseline
or scope launch. Subsequent PLL read failures followed the deliberate power
cut; they are not independent evidence of failed PLL lock. This attempt is
retained in `1160-startup-guard-rejected/`.

The follow-up reduces the control to **350 MHz, 1120→1100 mV**. It checks
sustained voltage margin while avoiding another PLL change at the reduced
voltage, and validates continuous PASS/FAIL, PVT and temperature observation.
It does not reproduce the earlier 400 MHz endpoint or establish behavior at
425/600 MHz. NVS retains 400 MHz / 1160 mV.

The first 1120 mV attempt rejected at the initial 100 MHz window: 97.86%
aggregate throughput, 97.29% minimum ASIC, and sampled rail maxima 1117/1124 mV.
It never reached a voltage-only launch. Evidence is retained in
`1120-initial-100-rejected/`. The follow-up allows a bench-only exception for
that first 100 MHz window when every ASIC has >=97% throughput and >=97%
pass rate. Later steps retain normal criteria. This changes a diagnostic
precondition, not production qualification or the voltage/temperature limits.

That exception let the next attempt reach 125 MHz, where the minimum ASIC
was again below the normal threshold (97.30%). It stopped with sampled
maxima 1136/1125 mV; evidence is in `1120-initial-exception-125-rejected/`.
The exception has now been removed. Instead of continuing the frequency ramp
after a rejection, the diagnostic captures the rejected window's frozen
per-domain counters, D2 PVT and temperatures, arms one new 180-second window,
and observes the rejected frequency for 90 seconds. A voltage guard trip or
invalid qualification communication still powers off immediately, without
entering that hold. Fresh temperature limits apply during the hold.

The new arm itself reapplies SPD and resets the check FIFO, so an immediate
change from the frozen window is not passive recovery. Only subsequent
samples in `rejected_hold` are free of further mining/clock configuration
writes. The parser treats the explicit zero-dt first sample as the new window
and does not interpret that intentional reset as a counter wrap. This branch
does not send a voltage change or capture its waveform; the host closes after
the hold ends. It permits measurement of a marginal state while preserving
the normal qualification criteria for advancing the frequency.

## Completed observation

The final run rejected at 100 MHz / 1120 mV and took the observation branch.
It completed the 90-second hold, with 88.92 seconds of complete counter
intervals after the first sample. It did not reach 350 MHz, change voltage,
or acquire a transition waveform. `result=COMPLETE` means the observation
finished, not that the rejected operating point became qualified.

| ASIC | D2 passing throughput, % ideal | D2 FAIL fraction | Temperature first→last | D2 PVT range |
| --- | --- | --- | --- | --- |
| A1 | 97.92% | 1.91% | 29.11→31.25°C | 279.04–279.45 mV |
| A2 | 98.22% | 1.70% | 28.94→30.92°C | 278.89–279.30 mV |
| A3 | 99.32% | 0.59% | 28.45→30.59°C | 281.84–282.19 mV |
| A4 | 98.44% | 1.33% | 30.43→32.57°C | 281.38–281.68 mV |

Errors were distributed across domains. A1 D0 averaged 96.45% passing
throughput with 3.47% FAIL; A2 D1 averaged 95.64% passing throughput with
4.12% FAIL. This is marginal hashing, unlike the earlier all-39-core D2 loss
at 400 MHz. There was no abrupt D2 PVT/throughput transition around 64 seconds
or elsewhere in this hold. It covers only a roughly 2°C rise near 30°C,
not the 50–60°C warm-restart regime described for 1102x.

The frozen pre-arm window had one A1 D0 core with zero PASS. The new window
still had one zero-PASS core through the sample 3.358 seconds after its first
A1 snapshot; none remained at 4.466 seconds. This establishes eventual passing
activity during an interval without another work/PLL strobe. It does not
identify the core, prove its clocks stopped, establish full recovery, or
prove the zero core in the two independently armed windows was the same one.
Per-domain FAIL totals cannot determine that individual core's error history.

The sampled guard maxima were 1117/1125 mV; ASIC power was switched off at
the planned end. This run has no scope waveform, so those maxima do not bound
fast startup excursions. All cumulative counters remained monotonic within
each observation window, and reconstructed interval rates matched the logs.

## Final device/source state

ASIC power is **off**. The device still contains the guarded diagnostic image;
rebooting it repeats the diagnostic startup/hold or waits for the host rather
than starting normal pool mining. Persistent NVS settings remain 400 MHz /
1160 mV. The source probe flag was returned to 0 and a normal image rebuilt,
but that normal image was not flashed: the earlier 1160 mV ramp exceeded the
1.20 V limit at 350 MHz. There was no unguarded restoration to 400 MHz.
All images/source snapshots and SHA256 hashes are retained locally.

The production counter reader now retains per-domain FAIL alongside PASS
from the same register transactions. It passed its transport-mocked C test
and live continuous reads. Diagnostic and disabled-probe firmware builds,
Python syntax/analysis checks and `git diff --check` completed successfully.

## Procedure

1. Use the normal broadcast startup qualifications through 350 MHz at 1120 mV.
2. Arm one 180-second SPDLOG window, then observe a 10-second baseline.
   Require at least three consecutive snapshots with every domain >=95%
   passing throughput before continuing.
3. Wait for the host to arm and force the scope, then send USB G.
4. Change only TPS546 Vcore from 1120 to 1100 mV per half-stack. No PLL,
   work, SPD or FIFO action writes occur after the counter arm in step 2.
5. Download the transition and accept it only if both half-stack sample peaks
   remain below 1.20 V, neither channel clips and the capture covers the launch.
   Paired chunks are checked as they arrive; the first failing chunk sends X.
   A missing/rejected verdict powers off; no automatic retry or live rollback.
6. If the capture passes, observe for another 90 seconds without intervention,
   then power the ASIC rail off.

The scope is armed only after qualification; startup PLL transitions are not
captured in this control. The independent 20 ms sampled rail guard remains active during qualification,
baseline and observation. Fresh invalid or >=70°C ASIC temperature readings
also end the observation. Temperature is read during snapshots after the
normal frequency ramp; it is not an independent high-rate thermal guard.
ASIC sensor-conversion writes continue, so this is not a write-free interface.
The normal power task skips ASIC sensor reads while initialization is pending.
The fan remains in the existing startup mode; no deliberate heating is used.

Each sample retains low-16 PASS and high-16 FAIL counters separately for all
156 cores, summarized by domain. Failed counter reads, decreasing cumulative
domain counts and invalid temperatures end the test. Per-core counts must
not wrap during the observation. At ideal 350 MHz activity and the selected
counter target, the 180-second arm is below one 16-bit per-core wrap. PASS=0
alone does not establish stopped clocks; PASS+FAIL counts quantify measured
hash-check activity, not a direct measurement of clock delivery.

The new fixed-voltage harness reuses the earlier `BCAST` serial prefix and
USB G/R/X protocol. The `voltage_only` phase distinguishes it from the earlier
PLL-transition experiment. PVT now covers D2 on all four ASICs and temperature
is sampled on all four. Sensor and counter reads are sequential, not simultaneous.

## Scope and analysis

Rigol LAN VXI-11 at 192.168.1.250; CH1=VDD−GND, CH2=GND0−GND,
upper=CH1−CH2. Both probes are configured 1×, DC coupled, 20 MHz bandwidth.
The shorter acquisition uses 700,000 samples/channel at 2 MSa/s, spanning
approximately −75 to +275 ms around a forced trigger before USB G. This has
less temporal resolution than the earlier 20 MSa/s trial. It resolves the
previous 0.4 ms pulse, but cannot establish the absence of all faster peaks.
No offset correction is applied to scope voltages.

The host checks each 250,000-point paired chunk before downloading the next.
This reduces verdict latency from the previous approximately 70-second
download; it remains software protection after the captured event, not
hardware overvoltage shutdown. A rejected capture is retained while the
remaining samples download with the ASIC rail off.

`analyze_fixed.py` checks counter continuity and the logged PASS rate formula,
calculates interval FAIL activity and phase summaries, and plots D2 throughput,
PVT and temperature. Phase averages omit the interval crossing the voltage
change. `test_counter_decode.py` compiles the production reader with synthetic
register responses to verify separate PASS/FAIL halves, bank/domain boundaries,
retry behavior, invalid input, read failures and the existing PASS-only tuning
path. Raw binaries and full logs are locally ignored; condensed evidence,
hashes, compressed scope samples and analysis results are retained.
