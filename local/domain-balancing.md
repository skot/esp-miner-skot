# bitaxeProto 1103x domain-balancing investigation

Last updated: 2026-09-05

## Production pool startup on 1103x-support (2026-09-05)

Fast-forwarded `1103x-balancing` through `c91844e` into `1103x-support`.
The production path now performs the successful warm startup automatically
and then hands over to ordinary pool mining. The older staggered diagnostic
and all new bench selectors are disabled. This supersedes the installed-image
and saved-setting descriptions in the historical sections below.

The combined sequence is: enumerate at 100 MHz / 800 mV per half, raise
voltage to 1160 mV, qualify through 300 MHz, warm all four ASICs, disable
both Vcore and VDDIO and verify discharge, reinitialize at 100 MHz / 800 mV,
raise voltage again, broadcast the PLL ramp without work or thermal
conversions, settle 1500 ms at 600 MHz, then obtain fresh temperatures and
qualify throughput before releasing pool work. Warmup temporarily leases
fan control without changing the saved fan settings. The stock 1103 TPS
profile remains 550 kHz, scale 0.25, compensation `13 11 8C 11 86`, with
scale configured before voltage commands/limits. The 1102 profile remains
separate at 1300 kHz.

The final production refinement clears synthetic qualification counters,
pending results and old valid-job flags before pool handoff. The job producer
retains the latest pool job while ASICs are unavailable and resends it after
reset, even without a new pool notification. These are software-state changes;
the cleanup does not rewrite PLLs. Persistent invalid temperature readings
after bounded retries now stop mining and force full fan speed.

Stock 1103 MAC 28:84:85:BA:96:08 completed the first **30-minute real-pool
hold at 600 MHz**, averaging **2949.43 GH/s** from ASIC counters. There were
**60 accepted shares during the hold, zero rejects**, and a maximum sampled
ASIC temperature of **55.17°C during the hold**. First fresh startup
temperatures were 27.62–28.28°C. No thermal retry, persistent thermal failure
or unexpected reboot occurred. This first run used the initial production
build. The handoff refinement then passed two five-minute holds at 2951.04
and 2955.93 GH/s, with 38 and 45 accepted shares during the holds and zero
rejects. Across the three holds, 40.09 minutes and 143 accepted shares were
recorded. These were successive build revisions, not one identical binary.

**Cold-resume repeatability remains unresolved.** At the user's request,
paused fan control was fixed to honor manual 100% through the API. Three
minutes of verified 100% fan / ASIC power OFF cooled fresh initial readings
to approximately 27–28°C. One resume qualified at 2963.04 GH/s but could not
mine because an interrupted boot had returned from `app_main` before creating
mining and pool tasks. Proto now creates those waiting tasks after interrupted
boot; a focused test compiles the real startup tail and verifies the behavior.

The next cold-resume run with that fix failed ASIC qualification at 2773.80
GH/s: **A2 D0 had all 39 cores at zero PASS**, though it was healthy at the warm
300 MHz checkpoint. A2 achieved 74.42% ideal throughput; A1 was marginal at
97.48%. No pool work was logged during the unloaded ramp. The firmware stopped
and powered ASICs off. The cause has not been isolated; the latest recovery
path's successful pool handoff is still unverified on hardware. The planned
pause/resume from an established mining session was cancelled after this failure.

Final board state is paused, ASIC rail 34 mV, saved 600 MHz / 1160 mV, automatic
fan settings restored. All test supervisors have stopped. The final installed
build SHA256 is `826a43260f8bf1cfac837c1b07650e76124cdd5279d47c93fd217f6e6041edcd`.

No scope was attached. This establishes sustained mining on stock
capacitance, not absence of fast voltage transients. Runtime counters show
per-ASIC throughput; they do not continuously measure all 16 internal domains.
An isolated weak core remains compatible with the final qualification limits.
The individual contributions of warmth, full supply cycling, voltage and work
timing have not been isolated by these combined-sequence tests.

Artifacts and exact build identities:
`local/experiments/2026-09-05-1103-pool-startup/README.md`.

## Stock 1103 result: 600 MHz / 2.965 TH/s without capacitor modifications (2026-09-05)

User connected a stock board that has mined before (MAC 28:84:85:BA:96:08,
board version 1103) and explicitly requested testing without scope probes.
The prepared scope image was superseded by a no-scope bench; no scope was
operated and no scope handshake was simulated. Board application/NVS/OTA
backups were saved before flashing; persistent settings were retained.

The first new-image boot could not enumerate ASICs: actual TPS output stopped
at about 1.208 V despite 1.600–1.920 V commands. Restoring this board's own
old application restored the expected output and all four ASICs. A PMBus-only
ON_OFF_CONFIG=0x1B comparison still failed on its first boot. Rebooting that
identical image then produced 1.606 V at the initial 1.600 V command and all
four ASICs enumerated. Thus GPIO enable alone did not explain the failure.

The TPS driver programmed voltage commands/limits before changing
VOUT_SCALE_LOOP from the older 0.125 setting to 0.25. The fix verifies
OPERATION=OFF and programs/verifies scale before the voltage configuration.
The test deliberately seeds 0.125 first, then applies 0.25 using the new
order. With the GPIO enable path restored (ON_OFF_CONFIG=0x1F), its first
boot measured 1.606 V and enumerated all four ASICs. This supports the
configuration-order explanation; the exact internal DAC/limit mechanism is
an inference. No TPS NVM was written.

The identical-reboot comparison warmed and qualified 300 MHz at 1486.42 GH/s
(99.25%), then stopped during the full supply cycle because sequential TPS
and ADC samples described different points on the rising rail. The revised
bench still checks fresh TPS and raw ADC read success during that bounded
cycle, but resumes derived half-stack voltages after supply settling and
1500–1700 mV total verification. Discharge verification remains <=100 mV.
Fresh TPS reads now also reject failures reading the VOUT_MODE exponent.

The combined probe completed the 300 MHz warmup, 1160 mV per half,
full ASIC power cycle and no-work ramp to 600 MHz. The 120.325-second
observation measured **2964.58 GH/s** (98.978% ideal); final 30 seconds
2964.76 GH/s. A1/A2/A3/A4 averaged737.81/741.14/742.61/743.03GH/s.
A1 D2 averaged96.438% with one persistent zero-PASS core, also seen during
warmup; the other15domains averaged98.890–99.451%. No whole-domain collapse
occurred. The bench completed, but `healthy_all=0` records the small A1D2
deficit; this is not an all-domain qualification pass.

All four temperature streams remained fresh, with zero invalid readings.
Maximum600MHz temperature60.12°C; final59.95/58.63/59.46/59.13°C.
Sampled PCB maxima1184/1180mV, total2325mV; no fast transient claim is
possible without scope probes. The firmware powered ASICs off at completion,
confirmed by subsequent36mV total. This is synthetic-work counter throughput,
not a pool accepted-share test. Stock capacitance was sufficient for this run;
repeat-start reliability and the remaining single-core deficit need testing.

The tested probe remains installed and repeats the diagnostic after reboot;
it does not hand over to pool mining. Saved settings remain450MHz/1160mV.
The new bench selectors and test-only scale seeding are disabled in working
source and the saved normal build, which was not flashed. The preexisting
staggered-PLL diagnostic remains enabled on this branch. Build and focused tests passed.

Artifacts: `local/experiments/2026-09-05-1103-scale-before-limits/`.
Earlier controls: `2026-09-05-1103-stock-no-scope`,
`2026-09-05-1103-stock-old-control`, `2026-09-05-1103-stock-pmbus-enable`,
and `2026-09-05-1103-stock-repeat-boot` under `local/experiments/`.

## Historical preparation: stock 1103 warm power-cycle comparison (2026-09-05)

Prepared and built, **not flashed or hardware-tested**. The next target is
stock 1103: original 330 µF polymer TPS output capacitor, without the added
100 µF half-stack capacitors. The successful 1102 warmup / full ASIC supply
cycle / no-work ramp to 600 MHz is retained, including 1160 mV per half.

Regulator selection is explicit: stock 1103 **550 kHz / scale 0.25 /
`13 11 8C 11 86`**, ceramic-modified 1103 **550 kHz / scale 0.25 /
`13 11 8C 19 86`**, 1102 **1300 kHz / scale 0.125 /
`13 20 C6 19 C6`**. Stock is the new default; the modified profile remains
available with `PROTO_1103_CERAMIC_OUTPUT=1`.

The selected 1103 bench retains midpoint ADC readings and makes voltage /
throughput thresholds report-only for comparison. Midpoint calculation is
skipped only during the bounded intentional power cycle at fresh TPS total
0–100 mV. Powered ADC errors still stop. New fresh TPS accessors prevent a
cached value after PMBus failure from being accepted as discharge/rail data;
ordinary cached telemetry remains unchanged. The original successful 1102
binary is preserved. Both builds and focused host tests passed.

Confirm the physical stock board, serial port and scope placement before
flashing. CH1 at TPS output must stay 50 Ω; CH2 is GND0−GND. The existing
+21 mV midpoint / +13 mV total offsets need checking on the target board.
All working-source bench flags are off; the saved probe requires the host
scope handshake and ends with ASIC power off.

Artifacts: `local/experiments/2026-09-05-1103-stock-warm-cycle/README.md`.
Probe SHA256: `542d0fcbbe0e7d0d8dab811117b5b5a471e8ac66bd8cdcfab1800b488620b49d`.

## Current-firmware result: 1102 recovers 600 MHz / 2.968 TH/s (2026-09-05)

**The full warm ASIC power-cycle comparison succeeded.** After 300 MHz
warmup, disable both Vcore and VDDIO, verify discharge (48 mV observed),
restart at 800 mV per half, reinitialize at 100 MHz, raise Vcore to 1160 mV,
then finish the historical 25 MHz PLL ramp without work or thermal reads.
After 1500 ms settling at 600 MHz, all four fresh temperatures were valid:
57.81/56.16/57.15/57.31°C. Send the first job only then.

The 120.758-second observation averaged **2967.73 GH/s**. All 16 domains
averaged **98.143–99.462% ideal**, with no zero-PASS cores in the measured
hold and no invalid temperatures during the run. Final temperatures were
59.79/58.47/59.62/59.46°C. The current strict thermal reader and broadcast
CFG/EN burst both worked. This establishes a working complete sequence,
not yet whether the essential difference is the rail cycle, startup voltage
trajectory, or their combination; repeatability also remains to be tested.

The scope captured the power cycle, full ramp and first work without clipping;
CH1 stayed 50 ohms. Raw peaks were TPS 2.58 V, lower half 1.78 V, upper
estimate 1.28 V, at 100 µs sampling with uncorrected probe offsets. Successful
startup still includes transients, so peak presence alone is not diagnostic.

Firmware powered ASICs off after successful completion. The tested bench
image remains installed and requires its host scope handshake. Ordinary
source/build has all bench flags disabled; the successful sequence is not yet
an automatic production startup path. Next: integrate an explicit startup
mode, hand off to ordinary pool mining, and verify repeated cold/warm starts.

Artifacts: `local/experiments/2026-09-05-1102-full-power-cycle/README.md`.

## Positive control: exact old image reproduces warm 600 MHz / 2.969 TH/s (2026-09-05)

Restored the exact saved `v2.14.0-40-gcd82a9e-dirty` application to this 1102,
keeping 600 MHz, 1160 mV per half and saved fan settings. After warming and
one clean ESP software reboot, the final eight SPDLOG windows averaged
**2968.79 GH/s (99.12% ideal)**: A1/A2/A3/A4
743.01/741.21/742.20/742.37 GH/s. All four temperatures continued to update
through more than 120 seconds; final values were
61.27/59.95/60.78/60.45°C. This independently reproduces the user's result.
The old image has per-ASIC throughput, not the newer per-domain counters.

The successful reboot also showed TPS peaks to 2.56 V and lower-half peaks
to 1.74 V in an unclipped 100 µs scope capture. These are uncorrected probe
values and can miss faster events. The presence of a transient alone does
not distinguish failed startup from successful startup. The waveform also
confirms an ASIC supply cycle during ESP reboot, unlike the retained-rail
ASIC reset in the recent bench.

Two current-firmware comparisons did not restore operation:

- Restoring all seven historical 450–600 MHz PLL tuples still produced A3
  raw zero / DONE=0 / STATUS=3 at 550 MHz on all three attempts.
- Keeping thermal conversions out of the complete no-work ramp reached
  600 MHz, but A3 **and A4** then returned raw zero / DONE=0 / STATUS=3 on
  all three attempts. It stopped before the first post-reset job.

The vendor datasheet PLL table matches the historical tuples. The local
register map identifies thermal STATUS bit 0 as BUSY and bit 1 as LOCK;
STATUS=3 with DONE=0 remains an incomplete conversion. It is not proof of
failed hashing domains. The temperature clock is documented as 12.5 MHz
system clock / 2, so direct dependence on a hash PLL divider is unproven.
The clean old source configures sensors after the PLL ramp, but exact
configuration placement in the saved dirty image is not yet established.

The subsequent full ASIC supply-cycle comparison succeeded, as recorded above. Details:

- `local/experiments/2026-09-05-1102-old-image-control/README.md`
- `local/experiments/2026-09-05-1102-historical-pll/README.md`
- `local/experiments/2026-09-05-1102-deferred-thermal/README.md`
- `local/experiments/2026-09-05-1102-full-power-cycle/README-preparation.md`

## Earlier: temperature retries expose persistent A3 conversion failure (2026-09-05)

Implemented, built, flashed and tested the temperature-reader refinement on
1102. `MC3_read_temperatures` now drains DOUT and verifies DONE=0 separately
on each ASIC before triggering; waits for each ASIC's DONE=1; logs status and
raw data; and retries invalid results up to three times with 100 ms spacing
and a shared two-second budget. Serial transactions can slightly extend that
budget. A valid >=70°C result ends retries immediately; no cached reading
substitutes for persistent failure. Focused host tests and both builds passed.

The repeated 1160 mV/half deferred-work test qualified warm 300 MHz at
**1489.64 GH/s, 99.47%**. Temperatures immediately before reset were
55.17/54.18/55.01/55.01°C. All four ASICs reinitialized and locked through
550 MHz without new work. A3 was valid at 525 MHz (58.80°C). At 550 MHz,
**all three A3 attempts returned raw zero, DONE=0 and STATUS=0x3**, with
successful raw/status register reads. Other ASICs remained valid at roughly
58–60°C. There were no other invalid temperature samples across 224 samples
per ASIC. No interpretation of STATUS bit meanings is established here.

The final retry ended at boot 329.426 s; GPIO10 went low at 329.439 s.
This establishes a persistent incomplete thermal conversion over the retry
interval, not just a single invalid sample. It does not establish whether
hashing domains failed: the stop again preceded 575/600 MHz, the first
post-reset job and all post-reset throughput counters.

The scope prefill correction worked. The 700k-point, 50 µs record is unclipped
while powered; subsequent CH1 clipping follows deliberate shutdown. Measured
powered peaks were TPS 2.552 V, lower half 1.740 V and upper-half estimate
1.404 V. During the settled 550 MHz retry interval, TPS stayed about
2.328–2.360 V, median 2.344 V, despite A3's persistent conversion failure.
These are uncorrected scope values, with approximate serial alignment and
insufficient sampling for 20 µs averages or the fastest transients. They do
not prove that the earlier transient caused the sensor failure.

Final state: ASIC power off, fan 70%, scope STOP, CH1 **50 Ω**. The new
retry-enabled deferred-work probe remains installed and reruns on reset;
normal source bench flags are off. Complete evidence, implementation tests,
scope limitations and waveform are in
`local/experiments/2026-09-05-1102-thermal-retry/README.md`.

## Deferred-work firmware preparation (2026-09-05; subsequently tested)

**Initially built without flashing; subsequently flashed and tested with user authorization. See the latest bench outcome below.** The new disabled bench mode
keeps 1160 mV/half and the existing 300 MHz warmup. After warm qualification
and a scope handshake it pulses ASIC reset low for 100 ms, retains Vcore and
VDDIO, re-enumerates four ASICs at 100 MHz, and ramps explicitly in 25 MHz
steps to 600 MHz without WORK_CFG/job writes or SPDLOG arms. Current divider
tuples and CFG/EN burst remain unchanged. Steps wait at least 500 ms through
500 MHz, 1000 ms at 525–575, and 1500 ms after final 600 MHz lock. Temperature
checks continue and can add inter-step time.

The same synthetic qualification job is the first job after reset, sent only
after final settling. One 180-second counter arm supports a 120-second
observation, then automatic ASIC power-off. This is an ASIC reset with the
regulator on, not a reproduction of the old ESP warm reboot's rail cycle.
Voltage remains report-only; temperature, communication, manual stop and
acquisition integrity checks remain active. The prepared long scope capture
covers reset/ramp/first work at reduced sample rate and is not suitable for
quantitative comparison of the previous fast peaks.

Both builds and focused host tests passed, including no premature work/arm,
settling minima, failure handling and long-capture coverage/clipping. No miner
or scope commands were issued during preparation. The installed 1160 mV
first-zero hold firmware is unchanged. Source bench flags are restored to zero.
Prepared binary, source snapshots, host supervisor, tests and hashes:
`local/experiments/2026-09-05-1102-deferred-work/README.md`.

## Startup review: older 1102 firmware (2026-09-05)

The same 1102 hardware has repeatedly achieved 600 MHz / 3 TH/s according to
the user. A source and saved-binary comparison found that the current tests
**changed PLL divider tuples at all seven 25 MHz points from 450–600 MHz**.
The complete historical 37-entry table matches 296 bytes in the exact old
factory backup at offset `0x36F18`, independently of its dirty source label.
For example, 475 MHz changed FBDIV/POSTDIV1/POSTDIV2 from **152/7/0 to
152/3/1**, and 600 MHz from **144/5/0 to 144/2/1**. At 525/550 MHz FBDIV
also changed. These alternatives have the same intended MHz but have not
been shown equivalent in startup behavior. The table change starts just below
the recent 475 MHz failure; it cannot alone explain the older 425 MHz failure,
whose tuple is unchanged.

Other material changes: qualification work now starts before the voltage
ramp; each bench PLL step is bracketed by WORK_CFG reset/apply and another
work/SPD/FIFO arm; mining configuration begins about **1 ms after 475 MHz
lock**, whereas the old successful log first configured pool work **1544 ms
after final 600 MHz lock**. CFG and EN=1 are now one burst packet, reducing
stopped-PLL time. Recent tests also used 1140 mV/half versus old 1160 mV.
The old actual firmware already had the 800 mV/100 MHz voltage ramp and
31.75 ms soft start, despite those being absent from clean `cd82a9e`.
Do not treat rebuilding that clean commit as reproducing the dirty image.

The seven tuple changes and PLL burst arrived in `be978c3`; early work and
per-step qualification arrived in `0ac9d30`. The generic ramp now traverses
49 extra fractional frequencies between 400 and 600 MHz; current bench +25
MHz loops do not, but simply reusing the generic ramp would not restore the
old sequence. TPS 1300 kHz/scale/compensation match the old 1102 setup;
GPIO enable and final ON_OFF_CONFIG differ (0x1B old, 0x1F new).

Next targeted comparison: historical divider tuples only, with the same
warmup/voltage/work sequence and first-zero hold. An exact old-image warm
restart at 1160 mV provides a separate positive control. This review made no
firmware changes and ran no hardware test. Full comparison and evidence:
`local/experiments/2026-09-05-1102-startup-review/README.md`.

## Latest bench outcome

**The prepared deferred-work firmware was flashed and tested, but stopped at
550 MHz before sending its first post-reset job.** Warm 300 MHz qualified at
99.34% / 1487.72 GH/s. At 287.903 s the ASIC reset pulse was applied with
1160 mV/half and VDDIO retained; four chips re-enumerated at 100 MHz. All PLLs
locked at each explicit 25 MHz step through 550 MHz. No mining config, target,
version base or SPDLOG arm was logged after reset; FIRST_WORK was never reached.

After 550 MHz lock, A3 returned **−288.90°C** at 300.073 s. The retained sensor
check powered off by 300.089 s. Last valid A3/A4 temperatures at 525 MHz were
58.63/58.80°C. This repeats the invalid sensor value seen previously at 600 MHz,
but now before new work, so it is not specific to immediate work reapplication.
There are no post-reset domain counters, no 600 MHz work and no final hold;
this run cannot establish domain health or resolve the work-timing hypothesis.

The long scope acquisition also failed: it never completed, and after the
supervisor stopped it both channels reported zero acquired points. No waveform
or transient amplitude is available. Trigger/acquisition handling needs fixing
before reuse. Serial sequence evidence is intact. No extra miner run followed.
ASIC power is off, final TPS 44 mV, fan 70% / 2727 RPM, CH1 **50 Ω**, scope STOP.
The deferred-work image is installed and reruns on reset; source flags stay zero.
Full result: `local/experiments/2026-09-05-1102-deferred-work/README.md`.

### Previous result: 1160 mV first-zero hold

**Raising only the bench voltage from 1140 to 1160 mV/half moved the first
failure from 475 to 500 MHz; it did not restore 600 MHz operation.** Source
comparison verified a single changed voltage constant, with identical PLL,
work timing, regulator, fan and first-zero hold code. Warm 300 MHz qualified
at 99.33%; all domains passed through 475 MHz (2354.21 GH/s, 98.59–99.79%
per domain).

At 500 MHz the same A1 D1 and A2 D2 domains became all-39-core zero PASS.
They produced no passing results during the complete **120.950-second hold**
with unchanged PLL/work/voltage and counter epoch. The other 14 domains
sustained 98.95–99.91%, about 2.171 TH/s total versus 2.496 TH/s ideal.
A2 D2 stayed at 238.76–239.11 mV, temperatures 56.82–58.30°C. No higher step
was attempted. Firmware powered off normally after the observation.

The 500 MHz lower-half transient was 1.780 V raw / 1.7085 V sliding-20-µs
mean; upper estimate 1.452 / 1.3001 V; TPS raw maximum 2.560 V. No clipping;
voltage thresholds remained report-only. The healthy 475 MHz step also had
large transients (lower 1.760 / 1.6955 V). Higher voltage improved observed
margin in this one comparison, but lower bench voltage alone does not explain
the gap from historical 600 MHz. Work timing remains untested at fixed 1160 mV.
Warmup was faster but used the same temperature gate and fan policy.

ASIC power is off; final TPS 46 mV, fan 70% / 2729 RPM, CH1 **50 Ω**, scope
STOP. The 1160 mV hold image remains installed and reruns on reset; source
bench flags are zero and normal firmware was built but not flashed. Builds,
focused host tests and hold-integrity checks passed. Full result:
`local/experiments/2026-09-05-1102-1160-zero-hold/README.md`.

### Previous result: 1140 mV two-minute hold

**1102 did not recover during a two-minute hold at its first zero-PASS failure.**
After the same 300 MHz warmup (99.26% qualification), A1 D1 and A2 D2 failed
at 475 MHz. The PLL, work configuration and voltage were held unchanged for
**120.930 seconds from detection**, with no counter rearm. All 39 cores in each
failed domain remained at zero PASS while FAIL counters increased. The other
14 domains sustained 98.93–99.69% throughput over the last 30 seconds, about
2.061 TH/s total versus 2.3712 TH/s ideal.

A2 D2 remained near 235.5 mV; temperatures stayed around 57–58°C. A1 D2
remained near 290 mV, but its failed domain was D1, whose voltage was not
measured. No temperature/sensor fault ended this run: the full hold completed
and firmware powered off normally. The original 180.020-second counting
window covered the hold in a single epoch on every ASIC.

The 475 MHz launch still produced lower-half 1.740 V raw / 1.6650 V sliding-
20-µs mean, upper estimate 1.420 / 1.2708 V, and TPS raw max 2.504 V. Voltage
limits remained report-only. This run does not reproduce the earlier 1103
64-second spontaneous recovery and shows two minutes alone was insufficient
here. It does not prove permanent failure or rule out a delayed identical-work
reset; no recovery strobe was sent.

ASIC power is off; final TPS 46 mV, fan 70% / 2754 RPM, scope STOP, CH1 **50 Ω**.
The first-zero hold image remains installed and reruns on reset; source bench
flags are zero and normal firmware was built but not flashed. Both builds,
focused host tests and run-integrity checks passed. Results and plots:
`local/experiments/2026-09-04-1102-zero-hold/README.md`.

### Previous result: 1102 report-only ramp to 600 MHz

**1102 reached 600 MHz PLL lock with voltage/throughput checks report-only,
but did not sustain 600 MHz or demonstrate 3 TH/s.** The user requested this
unrestricted ramp while retaining 300 MHz warmup. Warm baseline qualified at
99.20% / 1485.68 GH/s; all domains remained productive through 450 MHz.
At 475 MHz A1 D1 and A2 D2 went to zero PASS on all 39 cores. At 525 MHz both
A1/A2 had D1 and D2 at zero PASS; at 575 MHz each also lost D3, leaving about
1.783 TH/s in qualification-counter throughput. A3/A4 remained near 99%.
Failed domains still recorded FAIL activity and did not recover in later
recorded steps through 575 MHz.

All four PLLs locked at 600 MHz, but about 0.48 seconds after LAUNCHED the
first A3 temperature read returned **−288.90°C**. The retained sensor-integrity
check powered off before the planned 120-second fixed-frequency hold. No
600 MHz counter sample exists. This invalid sensor result is not evidence of
physical overheating; the raw sensor code and cause are unresolved.

Scope lower-half peaks rose from 1.660 V raw / 1.610 V sliding-20-µs mean at
325 MHz to **1.840 / 1.757 V at 600 MHz**; TPS raw maximum reached 2.576 V.
Voltage thresholds did not stop the run. CH2 range was widened to 0.5 V/div
(20 mV sample quantization), and no capture clipped. CH1 remained **50 Ω**.
Warmup alone did not prevent failures under this sequence at temporary
1140 mV. This is not equivalent to the old firmware's successful warm startup
at saved 1160 mV, and the compound launch does not isolate PLL from work strobes.

ASIC power is off; TPS 44 mV, fan 70% / 2754 RPM, scope stopped. **Report-only
probe image remains installed and reruns on reset.** Source probe/report-only
flags are restored to zero; normal firmware was built but not flashed. Both
builds and focused host policy tests passed. Full evidence and domain heatmap:
`local/experiments/2026-09-04-1102-report-only/README.md`.

### Previous result: guarded 1102 warm ramp

**1102 reproduces the transient with the matching new test sequence.** The
prepared image was flashed to confirmed board 1102; live TPS readbacks were
1300 kHz, scale 0.125, compensation `13 20 C6 19 C6`. Warm 300 MHz passed at
99.49%, followed by a scope-accepted baseline. At approximately 54–55°C, the
300→325 MHz launch produced **1.644 V raw / 1.5770 V sliding-20-µs lower-half
peak**, versus 1.424 / 1.4071 V on ceramic 1103. The upper estimate peaked at
1.416 raw / 1.3139 V averaged. The host rejected and powered off.

TPS output movement remained very similar: 1102 **+113.52 / −119.48 mV** from
its 2.29968 V baseline, versus ceramic 1103 +109.73 / −124.07 mV. At the 1102
lower peak, simultaneous total/lower/upper-estimate means were
2.4132/1.5770/0.8362 V. Midpoint redistribution remains substantial and was
larger in this 1102 capture despite its different output filter and 1300 kHz
regulator setup. These are separate single-run captures, not an isolated
component or switching-frequency experiment.

D2 A1/A2/A3/A4 throughput was **100.76/98.89/98.81/97.78%** over about 2.88 s
after launch, with no sampled zero-PASS cores in any domain. No ten-second
325 MHz qualification or higher step occurred. This shows the event is not
unique to 1103 hardware, but does not prove it occurred with the old 1102
firmware's different startup sequence or that these excursions are harmless.

The original July 24 dirty firmware was backed up in full before app-only
flashing. A second reset was needed to obtain the boot log; cause of the first
no-output boot was not established. NVS and boot selection were preserved.
The 1102 firmware has no midpoint ADC: its 20 ms total-rail guard saw only
2275 mV, missing the fast event. Shutdown logged GPIO10 low and PMBus OFF.
Final regulator telemetry was 46 mV; ASIC power is off, fan 70% / 2729 RPM.
Saved settings remain 600 MHz / 1160 mV and minimum fan 25%. Guarded 1102 image
remains installed; CH1 stays **50 Ω**, CH2 1 MΩ, scope stopped.

Evidence, old-image backup, firmware identity, raw captures and three-board
comparison plot: `local/experiments/2026-09-04-1102-warm-ramp/README.md`.

### Previous result: 1103 ceramics and revised compensation

**Ceramics plus the revised compensation did not materially reduce the warm
300→325 MHz transient.** The user replaced C4 with 4 × 100 µF ceramics and
changed compensation to `13 11 8C 19 86`. The guarded firmware was built and
app-flashed successfully; TPS readback confirmed those bytes, 550 kHz switching,
and 0.25 voltage scaling. CH1 stayed at 50 Ω on the TPS output caps throughout.

Warm 300 MHz qualified at 99.55% (1490.85 GH/s), PCB rails 1133/1147 mV, and
the scope baseline passed. The 300→325 MHz launch at approximately 53–56°C
produced TPS output sliding-20-µs excursions **+109.73 / −124.07 mV** relative
to its 2.29927 V baseline, versus **+102.90 / −115.30 mV** with the polymer.
Lower/upper-estimate raw peaks were **1.424 / 1.332 V**, and sliding-20-µs peaks
**1.4071 / 1.2804 V**. Both existing limits failed; firmware powered off after
host X. The sampled PCB guard saw only 1136/1150 mV and missed the fast event.

D2 throughput A1/A2/A3/A4 remained **100.86/99.17/99.23/98.19%** over about
2.93 seconds of complete intervals after launch; no domain had sampled
zero-PASS cores. No ten-second 325 MHz qualification or higher step occurred.
This reproduces the voltage transient without a persistent D2 failure in the
short observation. Both hardware and compensation changed together, so their
individual effects are not isolated. The capture does not verify the report's
6.147 mV switching-ripple prediction; CH1 sample resolution was about 8 mV.

ASIC power is off, fan 70% / 2727 RPM, scope stopped, CH1 **50 Ω**. NVS remains
400 MHz / 1160 mV with minimum fan 99%. The new guarded image remains installed
and reruns on reset. Source warm-probe flag is restored to zero; a normal image
with the revised compensation was built but not flashed. Full logs, source/bin
snapshots, scope data, hashes and aligned plots:
`local/experiments/2026-09-04-ceramic-warm-ramp/README.md`.

A next comparison should separate a work/configuration reset at unchanged PLL
from a PLL-frequency update; the compound launch does not identify each load
edge. It was not attempted in this run.

### Earlier 1102 firmware preparation (before the test above)

The user corrected the requested 1102 TPS switching frequency to **1300 kHz**.
Prepared board-specific selection restores its recorded compensation
`13 20 C6 19 C6` and scale loop 0.125 while retaining 1103's 550 kHz ceramic
settings. The 1102 board entry is restored. Its schematic netlist confirms
GPIO10/PWR_EN connects to TPS EN/UVLO, but GPIO2/ADC1_CH1 is unconnected.

The saved 1102 warm-probe image keeps the same ASIC sequence and scope limits.
Without midpoint sensing, its 20 ms firmware guard checks only TPS total
voltage (<2400 mV), and reports PCB balance unavailable. Scope CH2 and CH1−CH2
remain necessary for half-stack transient checks. Shutdown uses GPIO low plus
PMBus OFF. This is not equivalent to 1103's sampled per-half voltage guard.

Both test and normal images were built; host tests of board selection, voltage
boundaries and shutdown failures passed. Source test mode is disabled; the
next probe target is 1102. The user has not connected 1102, and **nothing was
flashed or tested on hardware**. Firmware, provenance, source snapshots and
connection-time checks:
`local/experiments/2026-09-04-1102-firmware/README.md`.

### Previous result: polymer, CH1 directly at TPS output

**The transient was reproduced directly at the TPS546 output capacitors.**
The user moved CH1 to the output caps using coax and required 50 Ω termination.
Readbacks confirmed CH1=FIFT (50 Ω), DC, 1× before and after both captures and
at cleanup; it remains in 50 Ω mode. CH2 remains 1 MΩ at the midpoint.
The firmware was already warming at 300 MHz when the supervisor attached;
no miner reset, flash or regulator-setting change was issued.

At approximately 52–56°C, warm 300 MHz qualified at 99.54% and the baseline
passed. The repeated 300→325 MHz broadcast launch showed TPS-local CH1
baseline 2.29450 V, raw extrema 2.152–2.416 V, and sliding 20 µs extrema
2.17920–2.39740 V: **+102.90 / −115.30 mV relative to baseline**. The prior
CH1 capture had **+102.33 / −115.47 mV** excursions under the same analysis.
This close reproduction confirms the transient exists at the regulator's
output-capacitor node. It does not by itself prove a defective TPS546 or
identify which output-filter, control-loop, sensing or load effect causes it.

The lower half reached 1.420 V raw / 1.4038 V sliding mean. CH1−CH2 reached
1.3240/1.2684 V, though that subtraction now combines different measurement
locations and is an upper-half estimate. The host rejected and powered off;
the 20 ms firmware guard missed the fast event (maxima 1158/1148 mV).

D2 remained healthy over the available 2.94 seconds of complete intervals:
A1/A2/A3/A4 throughput 100.91/99.12/99.21/98.26% of ideal, with no sampled
zero-PASS cores in any domain. No step above 325 MHz or ten-second step
qualification occurred. ASIC power is off, fan 70% (2757 RPM), saved settings
unchanged at 400 MHz / 1160 mV and minimum fan 99%. The guarded warmup image
remains installed. Source warm-probe flag remains zero; no firmware edits
were made. CH1 stays at 50 Ω and the scope is stopped.

Evidence and the aligned comparison plot:
`local/experiments/2026-09-04-tps-local-50ohm/`.

### Earlier candidate output-capacitor comparison

C4's schematic specifies Chemi-Con APXF6R3ARA331MF61G (330 µF polymer,
manufacturer ESR max 10 mΩ), whereas the July 31 WEBENCH report models
APXE6R3ARA331MF80G at 14 mΩ. Verify the actual fitted part. The July 20 report
uses 4 × 100 µF Murata ceramics; both reports retain 2 × 47 µF ceramics.
Predicted ripple falls 41.503→6.854 mV p-p and switching-frequency output
impedance 3.929→0.811 mΩ, while crossover remains 7.043→7.225 kHz. These
are model results at 2.2 V/30 A with 320 nH, not measured transient improvement
on this 360 nH-labelled board. Recommended voltage-loop gain differs, but the
reports' displayed PMBus component settings are identical.

ESR can contribute to transient voltage, but C4's ESR is not in series with
all ASIC current; the parallel capacitor bank's impedance matters. Test local
low-ESR ceramics with effective DC-biased capacitance and compensation checked,
and measure both TPS output and midpoint response. Adding ceramics alongside
C4 is a distinct first comparison from replacing C4. No hardware or firmware
change was made during this review. Details:
`local/experiments/2026-09-04-c4-review/README.md`.

### Previous probe placement and measurement allowance

**Warm 300→325 MHz launched successfully, with healthy D2 activity afterward,
but a large voltage transient remained.** The user authorized a modest scope
allowance for baseline offset/noise: raw peaks below 1.25 V and every sliding
20 µs mean below 1.22 V. Firmware's sampled 1.20 V guard was unchanged. The
previous 1140 mV guarded firmware was reused without source edits or flashing.

After warming to approximately 52–56°C, 300 MHz qualified at 99.57%, with
1138/1142 mV PCB rails. The scope baseline passed. The subsequent broadcast
300→325 MHz launch produced raw peaks **1.428/1.328 V** and maximum sliding
20 µs means **1.4133/1.2755 V**, exceeding both revised criteria. Lower-half
20 µs block means exceeded 1.20 V for approximately 220 µs; this was not just
a small isolated baseline sample. The host stopped the test and powered off.
The sampled guard saw only 1138/1145 mV maxima and missed the fast event.

Three post-launch snapshots showed no zero-PASS core in any domain. Over two
complete intervals (about 1.94 seconds), A1/A2 D2 averaged **100.73/99.33%**
of ideal passing throughput, with PVT around 282 mV. Settled PCB rails returned
to 1137–1138/1142–1143 mV. Thus a large warm transition transient did not
coincide with a persistent D2 failure in this short observation. No ten-second
325 MHz qualification, recovery hold, or higher-frequency step occurred.
There is no matched cold 300→325 comparison, so warmth's effect is not isolated;
behavior at 400–600 MHz remains untested in this warm-ramp series.

**State after that earlier run:** ASIC power off, guarded 1140 mV warm-ramp image installed;
it reruns on reboot. Source warm-probe flag remains zero. Fan returned to 70%
(2699 RPM). NVS remains 400 MHz / 1160 mV, auto fan, minimum 99%, manual 100%,
target 60°C. API `actualFrequency=325` is retained state, not active hashing.
Full evidence and zoomed captures are in
`local/experiments/2026-09-04-warm-ramp-tolerant/`.

### Follow-up: main VDD also has a transient

The user correctly identified the CH1 total-rail excursion. Earlier language
calling the TPS546 output steady was too strong at this time scale. Using
simultaneous 20 µs windows, the lower-half peak has total/lower/upper voltages
2.4102/1.4133/0.9969 V, against a baseline 2.3097/1.1535/1.1562 V. Thus
main VDD rises about 100.5 mV while the lower half rises 259.8 mV and the
upper falls 159.3 mV. At the upper-half peak, total VDD instead falls to
2.2176 V while lower/upper are 0.9421/1.2755 V. Both supply response and
midpoint redistribution are present; the capture does not establish causal
direction. TPS transient response is a plausible contributor and is not
exonerated by its steady telemetry.

The current 1103 configuration is 550 kHz, scale loop 0.25, compensation bytes
13 11 8C 11 86, previously read back and derived from WEBENCH. It still needs
validation against the actual load step and installed output capacitance.
Confirm CH1's physical probe location and compare regulator-local output
against ASIC VDD before attributing the entire CH1 excursion to the converter
control loop. See the latest experiment README and
`step-325/total-midpoint-decomposition.json` for simultaneous values and the
TI datasheet reference. No hardware settings changed during this analysis.

### Earlier warm baselines with strict scope limits

**The first two warmups stopped at the 300 MHz baseline.**
Two guarded warmups reduced fan speed temporarily to 10–30% and reached
approximately 52–56°C after four minutes at 300 MHz. All D2 cores produced
passing results throughout the sampled warmup windows. At 1160 mV, cold/warm
qualification improved from 99.09% to 99.55%; warm PCB rails were
1155/1164 mV. A separate 1140 mV run improved from 98.90% to 99.50%, with
1138/1142 mV warm rails. Both warm qualifications measured about 1.49 TH/s
on the diagnostic workload. These are separate voltage conditions, not a
matched cold/warm comparison of higher-frequency initialization.

The scope baseline blocked both runs **before any PLL step above 300 MHz**.
At 1160 mV it measured raw peaks 1.224/1.216 V; at 1140 mV, 1.208/1.196 V.
These were isolated recorded samples, with maximum 20 µs block means below
1.20 V, rather than the earlier millisecond-scale transition plateau. Scope
means were roughly 14–19 mV above board telemetry. At 2 MSa/s with 20 MHz
bandwidth, isolated samples do not establish pulse widths or prove a probe
artifact. The raw 1.20 V criterion was retained and the host powered off.
No higher-frequency transition, D2 failure/recovery, or 600 MHz operation
was observed. Baseline measurement/headroom needs validation before retrying.

At 1140 mV, A1 D0's FAIL fraction decreased from 2.18% in the first 30 seconds
to 0.56% in the last; A2 D1 decreased from 2.09% to 1.06%. D2 was healthy
from the start. Logger rearming twice during each warmup applies SPD/FIFO
strobes, so these are not completely passive thermal observations. The
recovery hypothesis remains promising but is neither confirmed nor refuted.
Details, traces and interval tables:
`local/experiments/2026-09-04-warm-ramp/` and
`local/experiments/2026-09-04-warm-ramp-1140/`.

After these earlier baseline-only runs, ASIC power was off and startup fan
control resumed. The later 325 MHz attempt above supersedes that state.

### Previous fixed-voltage control

**PASS/FAIL + temperature observation is implemented and bench-checked.**
A 90-second hold at a rejected 100 MHz / 1120 mV point showed A1/A2 D2 near
98% passing throughput, approximately 279 mV PVT, and temperatures rising
from about 29 to 31°C. Errors were spread across domains: A1 D0 and A2 D1
were worse than D2. No abrupt D2 recovery occurred. One A1 D0 core had zero
PASS through several seconds of the continuous window, then produced passing
results; the summaries cannot establish that core's full recovery or error
history. This lower-voltage marginality is distinct from the previous complete
D2 loss and does not test the 50–60°C warm-restart condition.

The intended voltage-only control at 400 MHz could not launch: its guarded
1160 mV startup crossed **1219/1100 mV during the 325→350 MHz update**, and
the guard powered off. Lower-voltage attempts rejected at 100/125 MHz, so the
final diagnostic observed the rejected state instead of advancing frequency.
Per-domain FAIL counts now come from the same packed reads as PASS; fresh
temperature and D2 PVT are recorded for all four ASICs. The scope controller
has shorter paired-chunk checking prepared, but no target transition or scope
capture occurred in these controls. Details and all rejected attempts are in
`local/experiments/2026-09-04-fixed-pll-voltage/`.

The earlier restored 400 MHz results below are historical.

**1102x warm-restart evidence adds temperature and reset history to the
startup hypothesis.** A user-supplied log from older firmware
`v2.14.0-40-gcd82a9e-dirty` shows A1/A2 near half throughput at 58.3°C before
an ESP reset, then all four chips near full throughput at 52.4°C afterward:
2.229→2.950 TH/s at the reported 600 MHz setting. Input power stayed connected;
the initialization log reads ASIC regulator output 0.05 V before ramping it
again. The user reports ambient-cold starts fail more often and warm restarts
succeed. Thus warming alone did not cure the observed bad state; reset at a
warm temperature changed the outcome. This favors investigating a startup
state that depends on temperature and history, without proving its mechanism.

The older sequence uses 25 MHz broadcast PLL steps, 500 ms waits through
500 MHz and 1000 ms above that, with the first pool-work configuration logged
1.544 seconds after the final 600 MHz lock. The reference commit has no
per-step qualification work/counter arm, unlike our diagnostic. Its running
image is dirty and differs from the user's current host build, so the commit
is not an exact source match. Voltage remains configured at 1160 mV per
half-stack. No internal domain counters or post-ramp midpoint readings are
included; the approximately half-rate failure cannot be assigned to D2 alone.
The healthy evidence is one short counter window, not a stability soak.
See `local/experiments/2026-09-04-1102-warm-log/`.

For the next comparison, record temperature alongside per-domain PASS/FAIL,
PVT and PCB voltages, and separate passive warming from reinitialization at
matched temperature using the same firmware and PLL/work sequence. The
1103x 64-second recovery below had no temperature trace, so it must not be
interpreted as a PLL-settling constant.

**Broadcast updates do not eliminate the D2 failure, and D2 can recover
without a second work strobe.** A broadcast 375→400 MHz launch at a temporary
1140 mV half-stack target left all 39 D2 cores on each of A1/A2 with zero
passing results for approximately 64 seconds. Fresh PVT measured D2 near
236–238 mV. Both domains then resumed near-full passing throughput and rose
to approximately 283 mV without another PLL/work/SPD/FIFO write or counter
rearm. The last complete intervals averaged A1 D2 99.01% over 4.84 seconds
and A2 D2 98.46% over 5.81 seconds. This is one recovery observation, not a
validated 64-second settling delay. Per-core FAIL counters were not retained;
zero PASS does not establish that hash clocks stopped. PVT conversion-control
writes continued during observation, so "passive" means no mining/clock
configuration intervention, not a completely write-free ASIC interface.

**Lower voltage alone did not make the transition safe.** The 1160 mV setting
leaves 40 mV nominal headroom to the external 1.20 V limit; 1140 and 1120 mV
leave 60 and 80 mV. At 1120 mV the first baseline failed at 375 MHz with A1/A2
near 74% throughput; another start rejected at 100 MHz. At 1140 mV the actual
broadcast transition produced uncorrected scope peaks of 1.504 V lower and
1.348 V upper. Using 20 us block means, lower voltage exceeded 1.20 V for
0.40 ms (0.12 ms above 1.30 V). Broadcast shortened the excursion compared
with the earlier addressed test but did not remove it; conditions differ,
so this is not a controlled quantitative comparison. Scope total VDD also
had short 2.112–2.480 V excursions around a 2.320 V baseline.

The host rejected that capture and powered off after downloading it. The
20 ms guard saw only a 1173 mV maximum and could not detect the short pulse.
D2's recovery happened during that download; no FIFO-only, SPD-only, or
full-work recovery intervention was sent. The initial scope record cannot
establish what electrical event occurred at recovery 64 seconds later.
Results and reproducible analysis are in
`local/experiments/2026-09-04-broadcast-1140/`; rejected lower-voltage baselines
are in `local/experiments/2026-09-04-broadcast-1120/`.

**Clock-delivery refinement tested: `pll_clk_on` is not an effective gate in
this configuration.** At 100 MHz / 960 mV per PCB half-stack, both a live
`EN=3→1` change and a complete `EN=0→CFG(125 MHz)+EN=1` restart left all four
ASICs hashing while their registers read `EN=0x5` (enabled, locked, clock-on
bit clear). Pass+fail activity increased from approximately 125 to 158 GH/s
per ASIC at the new divider **before** the subsequent EN=3 broadcast.
Thus waiting for all locks at EN=1 cannot coordinate actual clock delivery
through this bit. The low-voltage baseline had hash errors, so these are
clock-activity measurements, not mining qualification. No 425 MHz test was
performed. The scope measured peaks of approximately 1.072/1.064 V in this
low-voltage probe; this does not establish safety at normal mining voltage.
Evidence and method are in
`local/experiments/2026-09-04-clock-delivery/`.

The next firmware investigation needs a vendor-confirmed mechanism that
actually withholds hash clocks (including any required transfer handshake),
or a supported frequency-change method that keeps the active clock running.
Do not infer either from PLL/SPD register readback alone. In particular, the
mining-process application note says to keep PLL_EN bit 1 at its default 1,
whereas the abridged register map labels it a clock-output enable.

**LAN scope capture confirms the transient imbalance.** The connected Rigol
reports DS2302A (DS2000A family), with CH1 = VDD−GND and CH2 = GND0−GND.
During one guarded addressed 400→425 MHz launch, measured lower/upper peaks
were approximately **1.476/1.468 V** at different PLL updates. One half-stack
sat near **1.40 V for 4–5 ms while the other fell near 0.94–0.96 V**; total VDD
remained near 2.35 V aside from much smaller edge excursions. These are real
scope-observed shifts, not just an ADC concurrency artifact. The first priority
is avoiding unmatched half-stack loading while individual PLLs are stopped.

**No minimum settling delay has been established.** The delay sweep was stopped
because the faster rail guard detected approximately 1.37 V on a half-stack
during addressed PLL changes, including a 400→425 MHz transition **before the
requested 1000 ms wait or full work reapplication began**. The guard disabled
ASIC power. Neither increasing the cutoff nor continuing through the excursion
was attempted. The sweep is compiled out in the restored normal firmware.

There was also an instrumentation confound in the earlier runs: arming SPDLOG reapplies SPD clock
settings and resets the check FIFO. Recovery after counter rearming does not
prove that a second full work reapplication is necessary. The latest long
window avoided rearming and observed recovery without another work strobe,
but does not establish a reliable time-only fix. See the follow-up sections.

## Summary

The bitaxeProto 1103x can run all four MC3 ASICs normally through 400 MHz,
but a frequency transition near 425 MHz can leave A1 and A2 with greatly
reduced throughput. The loss is concentrated in the MC3 internal D2 voltage
domain, which contains slices 6–8: 39 of the ASIC's 156 small cores, or exactly
one quarter of its hashing resources. A3 and A4 normally remain healthy.

The earlier evidence points away from a simple lack of voltage or a permanent
hardware defect. After a failing 425 MHz launch, broadcasting the same work
configuration again, without changing any PLL divider, restores A1/A2 D2 to
normal throughput. It then remains healthy under continuous load. Our leading
hypothesis is therefore a timing or state-initialization problem between a PLL
frequency update and subsequent MC3 `WORK_CFG` actions. The follow-up on
2026-09-04 identified an important measurement confound: starting an SPDLOG
window also reapplies SPD settings and resets the check FIFO. The earlier
recovery experiment did not isolate full work reapplication from those actions.

One candidate firmware fix is to let the newly programmed PLLs settle before
the final broadcast work reset/activation. A delayed second work reset is a
known-effective fallback, but the required delay and minimum production
sequence still need to be measured.

## Hardware and symptom

- Hardware under test is bitaxeProto 1103x with four MC3 ASICs.
- The TPS546D24S supplies the two series half-stacks. At a configured
  1160 mV per half-stack, its total output is approximately 2.32 V.
- The PCB midpoint is `GND0`. The two external measurements are
  `GND0 - GND` and `VDD - GND0`.
- Each MC3 additionally contains four internal series voltage domains, D0–D3.
  Each internal domain contains three slices and 39 small cores.
- A bitaxeProto 1102x has generally reached 600 MHz and approximately 3 TH/s
  with this ASIC family, although genuinely cold starts have sometimes been
  troublesome.
- On 1103x, the first obvious failures were around 425–450 MHz. More granular
  testing also found marginal behavior around 434–438 MHz. In the repeatable
  400-to-425 MHz test, A1 and A2 lose throughput while A3 and A4 remain close
  to expected performance.

The external half-stack imbalance is not perfectly repeatable. Some failing
runs produced a large midpoint shift, while other runs showed poor D2
throughput with the two PCB half-stacks still balanced. This distinction became
important: the midpoint is useful, but it is not by itself a reliable test of
whether every internal MC3 domain is hashing.

## Instrumentation added

The firmware was extended during the investigation to provide:

- calibrated GPIO2 midpoint measurements;
- lower and upper PCB half-stack voltage after frequency steps;
- TPS546D24S current telemetry;
- per-ASIC internal VDD readings;
- all MC3 PVT voltage-sensor taps and calculated D0–D3 voltages;
- per-core SPDLOG counters grouped into slices and internal D0–D3 domains;
- addressed PLL changes for individual ASICs;
- fixed-PLL work-reset and long-duration recovery diagnostics.

This made it possible to distinguish an entire ASIC failure from the loss of a
single internal domain, and to distinguish an electrical voltage collapse from
a domain that has normal voltage but is not doing normal work.

## Experiments performed

### Power-supply and PCB changes

- Corrected the 1103 TPS546D24S configuration for its 550 kHz switching
  frequency and applied the WEBENCH-recommended compensation values.
- Corrected `VOUT_SCALE_LOOP` to 0.25 and verified readback of scale, switching
  frequency, and compensation registers after initialization.
- Added a 100 uF ceramic capacitor to each PCB half-stack.
- Replaced U7 and U10 with `TLV77308PDBVR` devices to test whether the floating
  VDDIO LDO arrangement was contributing leakage or bad regulation.
- Reflowed A1.
- Assembled and tested a second 1103x board.
- Replaced A1 internal-domain bypass capacitors C114–C121 with the older Taiyo
  Yuden 1 uF parts used on 1102x.

These changes did not eliminate the characteristic A1/A2 D2 failure at the
400-to-425 MHz transition. Extra half-stack capacitance did alter the shape of
the scope transient, but did not make the hashing failure disappear.

### PLL and ramp experiments

- Logged both PCB domains and TPS current at every 25 MHz PLL step.
- Tried smaller PLL steps between the normal table entries.
- Corrected `mc3_pll_config_t.frequency_mhz` to preserve fractional frequency
  values used by the fine-step table.
- Tested a nominally equivalent 450 MHz divider tuple
  `(FBDIV=144, POSTDIV1=3, POSTDIV2=1)` and extended that divider family above
  450 MHz.
- Compared addressed per-chip updates with normal broadcast PLL updates.
- Tried alternate chip-update orders across the two PCB half-stacks.
- Tried a ping-pong PLL experiment. The cores did not successfully live-switch
  to the alternate locked PLL, so this is not currently a viable solution.
- Shortened the interval for which the active PLL was stopped during a normal
  reprogramming sequence. Scope captures showed approximately 4.44 ms in the
  original sequence and approximately 2.72 ms after shortening it.

Reducing the PLL-off interval reduced the electrical disturbance, but did not
by itself make operation above the failure threshold reliable. The addressed
tests also showed that the problem is not simply the total load step seen by
the regulator.

### Voltage/frequency and load tests

- Tested several core-voltage/frequency combinations around the failure
  threshold.
- Held the board at a failing frequency and examined loaded and load-release
  behavior.
- Measured internal capacitors directly with a DMM and captured both PCB
  half-stacks plus their difference with a two-channel scope.
- Added a safety limit that terminates diagnostics if either PCB half-stack
  reaches 1.20 V.

Increasing capacitance, changing the LDOs, reflowing A1, and changing static
voltage did not provide a consistent cure. Frequency-transition and work-launch
history were more predictive than the settled regulator voltage.

## Internal-domain findings

At a healthy 400 MHz baseline, all four ASICs produce approximately 99% of
expected throughput. Their internal PVT domain voltages are near 0.29 V and
their per-core counters show activity in all four internal domains.

In some earlier 425 MHz failures:

- A1 and A2 fell to roughly 75% total throughput;
- all 39 cores in D2 reported zero passes;
- A1/A2 D2 PVT voltage fell to about 239–240 mV;
- the external PCB split increased to approximately 59 mV.

That looked like a complete internal-domain collapse. However, the latest
clean comparison exposed a more nuanced version of the same failure:

- all four ASICs were healthy at 400 MHz;
- A1, A3, A2, and A4 were changed to 425 MHz back-to-back in approximately
  74 ms;
- the normal identical-work reset was issued immediately after the final PLL
  update;
- A1 reached only 82.14% and A2 only 81.94% of expected throughput;
- A1/A2 D2 produced only about 36% of its ideal share of the ASIC's passes;
- A3 and A4 remained at approximately 99.2%;
- A1/A2 D2 still measured a normal 287.3–287.6 mV;
- the PCB half-stacks remained balanced within 9 mV;
- no D2 core had a zero counter.

This proves that the D2 throughput failure can exist without a persistent D2
voltage collapse. A large midpoint divergence is therefore probably a
consequence or more severe form of the bad domain state, rather than the only
root cause.

## Decisive fixed-PLL recovery test

After reproducing the latest A1/A2 D2 failure, the firmware performed exactly
one additional broadcast reset/reapplication of the same qualification work.
No PLL frequency or divider was changed. It then armed a 30-second SPDLOG
window and sampled A1/A2 D2 counters, PVT voltage, PCB midpoint, and TPS current
without further PLL or work updates.

Results after the additional work reset:

- A1 D2 reached 97.2% during the first reliable interval and then remained at
  approximately 98–101%.
- A2 D2 reached approximately 99% and remained at approximately 97–100%.
- At 30 seconds, cumulative D2 throughput was 98.6% for A1 and 98.8% for A2.
- A1/A2 D2 remained at approximately 287.1–287.6 mV.
- The external half-stack difference remained between 7 and 11 mV.
- TPS current remained approximately 14.16–14.33 A.
- No delayed collapse occurred during continuous 425 MHz work.
- The subsequent rollback to 400 MHz passed at approximately 99% per ASIC.

The first sub-second sample has timing skew because A1 and A2 counters are read
sequentially. It should not be used to compare the two ASICs precisely. The
later interval measurements use the same read order and clearly show sustained
full-rate D2 operation.

## Follow-up: measurement side effects and rollback safety

The first delayed-reset pilot used zero intentional delay and two consecutive
SPDLOG windows. Its measured gap from the final addressed PLL helper returning
to the work-reapplication call was 0.012 ms; the four PLL updates took 76.056 ms.

- First window: A1 D2 57.63%, A2 D2 56.17% of absolute expected throughput.
- Second window: A1 D2 99.53%, A2 D2 99.16%.
- The windows measured PCB rails at 1154/1165 and 1155/1164 mV.
- There was no second full work reapplication, but there **was** another
  `mc3_start_spdlog_window()` call between the windows.

The local *MC3 Abridged Register Map & Startup Pseudocode* identifies
`WORK_CFG.rst_fifo` and `WORK_CFG.spd_go` as write-one action bits. Its example
`0x8C00DA00` is the same value used by this firmware's SPDLOG setup. Thus,
counter arming is **not passive observation**: it writes `GLOBAL_SPD`, applies
SPD settings, resets the check FIFO, then resets/starts the counters. It does
not change the PLL divider, but can change the hashing state. The pilot does
not distinguish spontaneous time-dependent recovery from recovery caused by
this extra strobe. The previous 30-second timeline was passive only *after*
its initial counter arm.

The pilot also enabled an independent task sampling the PCB rails at roughly
20 ms intervals plus I/O time. During the old rollback path
(`MC3_qualify_chip_frequencies`, which quiesces work before sequential PLL
updates), it sampled **1373/946 mV** and immediately disabled GPIO10 ASIC power.
This happened during rollback, after both 425 MHz windows. It is an observed
limit excursion, not a claim that the 1.20 V limit was maintained. Earlier,
sparse measurements could have missed such excursions. Sampled monitoring
cannot bound shorter transients; scope verification remains necessary.

The second pilot used one counter window and attempted live rollback in
A1/A3/A2/A4 order. Its zero-delay launch left all A1/A2 D2 cores inactive;
the measurement reported 1130/1189 mV. Live rollback also triggered the guard,
at **943/1374 mV**, after A1 was lowered. Both in-place rollback methods were
therefore abandoned for this experiment.

The final sweep design uses one counter window per trial and **disables the whole
ASIC rail before an ESP restart between trials**. It never changes a failed
425 MHz profile back to 400 MHz under power. The next boot runs the normal
ordered startup and qualification ramp to 400 MHz. A baseline failure, a
sampled rail at or above 1200 mV, or missing voltage telemetry stops the sweep
with ASIC power disabled. An interrupted trial is latched against automatic
retry. Progress is held in RTC no-init memory, not persistent mining settings.

The planned coarse pass was 1000, 500, 250, 100, 50, 10, and 0 ms, one launch
each. These would be ASIC power cycles on a warm board, not fully cooled board
cold-start tests. Both per-ASIC throughput and absolute per-domain throughput
are checked; the latter must reach 97% with no zero-count cores. All trials
retain 1160 mV. Any promising delay still needs repeated trials.

With the ADC callers serialized, the 1000 ms trial passed its all-400 baseline
with every domain above 97%. A1 and A3 were then changed to 425 MHz and reported
lock. During A2's update, the guard measured **1373/946 mV** and disabled the
rail. A4 was not updated, no 1000 ms wait was performed, and no final work
reapplication or 425 MHz measurement occurred. The logged `measurement=ERROR`
is an aborted trial, **not a failed 1000 ms settling result**. The other nonzero
delays were not tested. The software-restart progression also remains untested
on hardware because no trial reached that part of the final harness.

This changes the experimental priority. Healthy settled PVT/PCB voltages
cannot exclude a short electrical excursion as the trigger for the bad state.
The addressed PLL transition must first be made safe and observed with a scope
before resuming a delay sweep. Once safe, a single long SPDLOG window with
counter-only reads can distinguish spontaneous recovery from recovery caused
by later `GLOBAL_SPD`, `spd_go`, FIFO-reset, or full work-reapplication actions.

### LAN scope confirmation

The user supplied the scope at `192.168.1.250`. Raw SCPI on port 5555 initially
answered queries but stalled following an unsupported math query. VXI-11 LAN
control with interface clear worked reliably; DS2000A math commands use
`CALCulate:SUB`, not `MATH`. The scope's subtraction was confirmed as CH1−CH2.

One further guarded launch used the same A1/A3/A2/A4 order and requested
1000 ms settling time. The scope was armed at the qualified 400 MHz baseline
and force-triggered at the trial marker (with a rising CH2 trigger as backup).
Firmware again stopped during A2's update, logging 1383/936 mV. A4, the delay,
and the full work reapplication were never reached.

| Voltage excursion | Inferred update from serial order | Time relative to scope trigger | Duration above 1.30 V | Median during excursion | Peak |
| --- | --- | --- | --- | --- | --- |
| Lower high / upper low | A1 | 1.00–5.42 ms | 4.42 ms | 1.408 V | 1.476 V |
| Upper high / lower low | A3 | 18.24–23.46 ms | 5.22 ms | 1.396 V | 1.468 V |
| Lower high / upper low | A2 | 36.28–40.32 ms | 4.04 ms, ended by shutdown | 1.404 V | 1.472 V |

Scope setup: DC coupling, 20 MHz bandwidth limit on both channels, 20 MSa/s,
28 Mpoint acquisition over 1.4 seconds. The saved paired raw data contains
the first 7 Mpoints/channel: −200 to +150 ms, covering every update and the
shutdown. Upper voltage is calculated directly from time-aligned CH1 and CH2
bytes and their per-channel preambles. Saturated samples after power-off are
excluded. Durations use 20 us block means and a 1.30 V threshold; ASIC labels
are inferred from serial order, not from a measured PLL clock signal.

These are uncorrected scope voltages. Before the transition the scope measured
approximately 2.352 V total, 1.184 V lower, and 1.164 V upper. Total and lower
readings are about 30 mV above firmware telemetry, so the instrument readings
should not be silently combined or treated as a calibration of one another.
That offset is far smaller than the observed 0.22–0.29 V excursions.

Files, analysis scripts, screenshots, and a PDF plot are saved under
`local/experiments/2026-09-04-rigol/`. The scope is left stopped and zoomed to
5 ms/div around the event. The board was again restored to normal 400 MHz:
99.16% startup qualification, about 1.98 TH/s live SPDLOG, and roughly
1153–1155 / 1164–1166 mV PCB telemetry. No production settling delay was added.

Raw logs and exact firmware binaries are retained locally in
`local/experiments/2026-09-04-reset-delay/`; CSV/JSON summaries and the parser
are retained alongside them. `sweep-boot1` is the aborted two-window pilot;
`sweep-boot2` is the aborted live-rollback pilot. `sweep-powercycles-v4` stopped
at its 400 MHz baseline because concurrent ADC callers caused a failed
midpoint read; no 425 MHz launch occurred. A mutex now serializes the complete
ADC averaging/calibration operation, retaining the fail-closed guard behavior.
The coarse power-cycled sweep is `sweep-powercycles-v5`.
The v3 firmware was replaced during its initial
startup, before any 425 MHz trial, to correct RTC progress retention.
Timing refers to the caller
boundary after the last PLL helper, not an oscilloscope measurement of the
on-wire `WORK_CFG` strobe.

## Current root-cause hypothesis

The current working hypothesis is an MC3 initialization or synchronization
problem during a frequency transition, with temperature and electrical margin
potentially determining which state the ASIC enters:

1. The PLL dividers are changed and lock is reported.
2. Work is reset/reactivated almost immediately.
3. A1/A2 D2 enters a bad hashing state even though its PLL and voltage can look
   normal.
4. The initial qualification window can be weak. In the reduced-voltage
   broadcast control, zero D2 PASS counts persisted for about 64 seconds,
   then recovered without another SPD/work strobe as D2 voltage also rose.
5. Later full work reapplication plus counter arming, or counter rearming
   alone, has been followed by full D2 throughput at 425 MHz. The necessary
   action and its timing have not yet been isolated.

The 1103 regulator, capacitor, or layout changes may reduce electrical margin
and make this race easier to trigger than on 1102x. They are not exonerated.
Earlier 425 MHz failures had healthy settled PVT; the reduced-voltage broadcast
failure instead had low D2 voltage that rose with recovery. We must explain
both observations. Fixed-PLL recovery argues against permanent loss of those
cores, but neither establishes the direction of causality between internal
voltage redistribution and valid hashing nor rules out supply margin.

It is also not yet clear why A1 and A2 repeatedly expose D2 while A3 and A4 do
not. Possibilities include chain timing, the A1/A2 PCB half-stack environment,
or lower margin in those two ASICs. The repeatability of the D2 grouping makes
an arbitrary quarter-core software-accounting error unlikely, but the exact
MC3 internal mechanism remains unproven.

## Candidate sequence, not yet a validated fix

After making the PLL transition safe, the candidate sequence to test is:

1. Quiesce or otherwise prepare work for the frequency transition.
2. Program all required PLL dividers.
3. Verify that every PLL reports lock.
4. Wait for a measured settling interval.
5. Broadcast one final `WORK_CFG` reset and reapply work only after that delay.
6. Qualify both per-ASIC throughput and per-domain core activity before
   proceeding to the next frequency.

The earlier fixed-PLL recovery used a second delayed full work reapplication
followed by counter arming. It was effective in that run, but the individual
SPD/FIFO actions still need isolation before treating this as a production fix.

The deferred experiment should sweep the delay between the last 425 MHz PLL update
and the first/final work reset, for example 0, 10, 50, 100, 250, 500, and
1000 ms. Each point should begin from a fresh qualified 400 MHz state and record
A1/A2 D2 interval throughput, PVT voltage, PCB midpoint, and TPS current. The
smallest delay that repeatedly starts all four domains near full throughput on
cold and warm boots should become the production settling delay, with some
margin added.

## Safety and current status

The TPS546D24S enable path was changed so the active-high enable GPIO is driven
low early and remains disabled while the ESP32 is in its bootloader/reset state.
This prevents the ASIC rail from remaining enabled during USB flashing. The
diagnostics also stop if either external half-stack reaches 1.20 V.

The automatic recovery diagnostic, delay sweep, clock-delivery probe,
broadcast recovery probe, fixed-PLL voltage probe and warm-ramp probe are
compiled out in current source. The latest guarded 1140 mV warm-ramp image
remains on the device with ASIC power off after the warm 300→325 MHz scope
rejection, even under the user-authorized measurement allowance.
The normal image was built but not flashed; the earlier 325→350 MHz startup
guard trip at 1160 mV remains unresolved. NVS still holds 400 MHz / 1160 mV.
The temporary fan lease has ended and startup fan control is back at 70%.
The latest TPS-local scope rerun reproduced the transient; CH1 remains 50 Ω.

Historical restoration before that latest control: the normal 400 MHz firmware
was rebuilt and flashed.
After the broadcast/recovery probe, all four ASICs passed startup qualification at
99.14% aggregate throughput (1979.59 GH/s), with a 99.01% minimum per-chip
result. Qualification measured 1154/1165 mV and 13.06 A; subsequent normal
operation measured approximately 1153–1154/1165–1166 mV and 1.98 TH/s by SPDLOG. Persistent voltage/frequency settings were
not changed. The build and whitespace checks passed. The retained changes
include ADC serialization, accurate counter-arm documentation/error handling,
and the disabled diagnostic harness; no production settling delay was added.
