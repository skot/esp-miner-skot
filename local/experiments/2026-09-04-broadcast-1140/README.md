# Broadcast transition and D2 recovery at 1.140 V

This follows the rejected 1.120 V baseline attempts in the sibling experiment
directory. It tests a broadcast 375→400 MHz transition, before another 425 MHz
attempt, to study the voltage-margin tradeoff and D2's recovery behavior.
NVS is preserved at its prior 400 MHz / 1160 mV settings.

## Observed result

The revised 375 MHz baseline passed. Following the broadcast 400 MHz launch,
A1/A2 D2 initially produced zero PASS counts on all 39 cores each. A3/A4 D2
and the other domains continued near normal throughput. Fresh A1/A2 D2 PVT
readings began at 237.03/238.25 mV, gradually declining toward 236 mV.
Settled PCB telemetry was about 1110/1170 mV, at approximately 13.1 A.

Both failed D2 domains recovered around 64 seconds after the first A1 counter
snapshot, without another work, PLL, SPD or FIFO intervention or counter arm:

| Domain | Last zero cumulative PASS sample | First nonzero sample | Fully recovered tail |
| --- | --- | --- | --- |
| A1 D2 | 63.838 s | 64.804 s | 99.01% over 4.84 s |
| A2 D2 | 62.962 s | 63.930 s | 98.46% over 5.81 s |

The first A2 nonzero interval had only 2.13% throughput; the next reached
98.77%. A1's first nonzero interval reached 90.84%. Both internal D2 readings
rose to approximately 283 mV. Samples are sequential, so this does not prove
simultaneous recovery. Tail averages deliberately exclude the first nonzero
interval and the following interval. The counter timer was 180.020 seconds,
and all recorded cumulative domain PASS counts remained monotonic.

Meanwhile, downloading the launch waveform revealed scope peaks of 1.504 V
lower, 1.348 V upper, and 2.480 V total (minimum total 2.112 V). Pre-trigger
medians were 1.164/1.156 V, total 2.320 V; these are uncorrected readings,
not identical to firmware telemetry. On 20 us block means, the lower rail was
above 1.20 V for 0.40 ms around trigger+35.6 ms, including 0.12 ms above
1.30 V. The upper rail had much shorter crossings of 1.20 V around +38.3 ms.
The capture was not clipped and covered the complete launch.

The host sent X after the scope audit; the firmware powered off with
`result=ERROR`, six consecutive healthy snapshots, and sampled guard maxima
1157/1173 mV. ERROR refers to the rejected voltage capture, not lack of D2
recovery. No recovery action was sent. The sampled guard missed these short
excursions. Download latency allowed about 71 seconds of observation before
that verdict; this is not real-time scope overvoltage protection.

This run demonstrates recovery without a second mining-configuration strobe.
It does not validate a 64-second delay, establish causality from the transient,
or prove the cores stopped clocking: the current per-core reader retains only
PASS, discarding packed FAIL counts. PVT acquisition continues conversion
control writes; only mining/clock configuration is left untouched during
the observation. No waveform was captured at the later recovery event.

The next diagnostic should retain PASS and FAIL separately, reduce scope
verdict latency, and examine a vendor-supported way to avoid PLL stop/restart
load steps before repeating high-voltage transitions. Lower voltage trades
overvoltage headroom against hashing margin; neither 1120 nor 1140 mV has
been established as a reliable fix. External half-stack measurements do not
determine where additional internal-domain capacitance would help.

## Protocol

The disabled bench harness temporarily sets the PCB half-stack target to
1140 mV. It uses normal broadcast qualification from 100 through 375 MHz;
all normal per-chip throughput, pass-rate, and PCB balance criteria must pass.
The final qualification's frozen per-core counters then establish the
baseline without another counter arm. D2 must reach >=97% of its ideal
throughput on every ASIC; no core may have zero passing work. Other domains
are recorded and must reach >=90%, while the normal global/per-chip criteria
remain unchanged. These are diagnostic preconditions, not new production
qualification thresholds.

An earlier attempt passed normal 375 MHz qualification and had D2 near 99%,
but the extra all-domain >=97% check rejected A2 D1 at 95.91%. That run is
preserved in `baseline-d1-rejected/`. The revised precondition targets D2 and
avoids the redundant extra SPDLOG arm in that baseline check.

The ESP waits at the accepted baseline for USB `G`. The host arms the Rigol,
waits for its WAIT state, forces the acquisition, then sends G. The launch
matches normal qualification: WORK_CFG apply, 10 ms pause, broadcast PLL
stop/configure/start, lock verification, identical qualification work, and
one SPDLOG arm with a 180-second timer. The longer timer is the observation
change from normal qualification; no further logger reset/rearm occurs.

The firmware repeatedly reads per-core counters on all four ASICs and fresh
A1/A2 PVT taps, then PCB voltages and regulator current. Per-chip counter
intervals use the midpoint timestamps of the respective UART read operations.
PVT samples precede the per-core reads and are not simultaneous with them.
Cumulative zero-core counts cannot by themselves identify a newly stopped
core; interval counter differences determine passing activity. FAIL counts
are not available from the current domain-summary helper.

The host downloads the matched raw scope channels while a separate thread
drains serial output. Recovery is permitted with USB `R` only if both measured
half-stack peaks are below 1.20 V, no channel clips, the record covers the
launch, and the firmware has not already ended. Otherwise X ends the probe.
Missing host commands time out and power off. An independent 20 ms sampled
guard also shuts off on invalid telemetry or either half-stack >=1.20 V.
The sampled guard cannot bound shorter excursions.

After the scope verdict, no intervention is made if all domain intervals
have been >=95% for three successive snapshots. Otherwise the sequence is:

1. FIFO-reset-only WORK_CFG write; observe for 8 seconds.
2. SPD-apply-only WORK_CFG write; observe for 8 seconds.
3. Full identical qualification work; observe for 15 seconds.

Later interventions are skipped once the three-snapshot recovery condition
is met. This condition only avoids unnecessary further strobes; the stored
longer-window counts are used to assess recovery quantitatively. FIFO and
SPD actions do not write PLL registers. Full work includes the same PLL_EN=3
write and restores the logging core target without resetting/rearming the
logger; PLL dividers are unchanged. A decrease in any domain counter aborts
the run rather than being interpreted as activity. Each attempt ends by
powering the ASIC rail off, with no live PLL rollback or automatic retry.

## Artifacts and limits

`capture_broadcast.py` configures the scope and handles the USB handshake; it
does not flash firmware. CH1=VDD−GND, CH2=GND0−GND, both DC coupled with 1×
probe factors and 20 MHz bandwidth limit. Memory is 7 million points/channel.
Voltages are decoded from each recorded preamble, with upper=CH1−CH2; no
offset correction is applied. Raw data retains the short transition; later
recovery phases have sampled firmware voltage protection, not new scope
captures. Source-call and serial timestamps are not direct clock-pin timing.

`analyze_broadcast.py` checks counter continuity, reconstructs domain rates,
exports JSON/CSV, and renders the D2 timeline and scope trace. Phase summaries
exclude the first interval crossing each intervention boundary. Sequential
interventions are conditional evidence, not independent randomized trials.
Large binaries, full serial/build logs, and exact source snapshots remain
locally ignored; condensed evidence and firmware hashes are included.

## Restoration and verification

The probe was compiled out, the normal application rebuilt and flashed,
and the original NVS 400 MHz / 1160 mV settings preserved. All ASICs qualified
at 400 MHz: aggregate 1979.59 GH/s (99.14%), minimum ASIC 99.01%, with
1154/1165 mV PCB telemetry and 13.06 A. Subsequent live SPDLOG measurements
were approximately 1.98 TH/s. `restored-400-evidence.txt` records the check.
The scope remains stopped with the failed transition retained.

Both firmware builds completed, app flash verification succeeded, analysis
checked every interval against the recorded rate and found no cumulative
counter decrease, Python syntax checks passed, and `git diff --check` passed.
No production settling delay or capacitance change was made.
