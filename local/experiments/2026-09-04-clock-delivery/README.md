# MC3 actual clock-delivery probe — 2026-09-04

Purpose: distinguish changing `PLL0_EN.pll_clk_on` on a running PLL from
withholding it during a complete PLL restart. This is a clock-activity test,
not a mining qualification or a validated high-frequency fix.

## Result

**EN=1 does not withhold the active hash clocks in either tested path.**
All four ASICs continued producing passing and failing work while their PLL0
register read `0x5` (enabled, locked, clock-on bit clear). After restart with
new 125 MHz dividers, activity rose before the later EN=3 broadcast.

| Stage | Readback EN | Average pass+fail activity per ASIC |
|---|---|---:|
| 100 MHz control | 0x7 | 125.47 GH/s equivalent |
| 100 MHz, live EN=1 | 0x5 | 127.13 GH/s equivalent |
| Restart into 125 MHz, held EN=1 | 0x5 | 157.81 GH/s equivalent |
| 125 MHz, released EN=3 | 0x7 | 155.83 GH/s equivalent |

Nominal activity is 124.8 at 100 MHz and 156.0 at 125 MHz. These short
~126 ms intervals include measurement variation. These are pass+fail core
activity rates, **not useful mining hashrates**. Passing-work rates alone
remained 60–114 GH/s per ASIC during the EN=1 intervals, independently
establishing continued hashing. See `counter-results.json` for each ASIC,
raw deltas, timing, and CFG/EN readbacks.

No counter arm or configuration strobe intervened between those snapshots.
This rules out the proposed “lock all PLLs at EN=1, then release together
with EN=3” mechanism in the tested configuration. It does not identify the
actual role of bit 1 or rule out a different, vendor-confirmed gating path.

No 400→425 MHz transition was attempted. The probe ended with ASIC power off;
the normal firmware was then restored for 400 MHz verification.

## Prior evidence and documentation

The earlier “Fix bitaxeProto voltage balance” investigation tested per-core
SPD gating, global SPD gating, core soft reset, and live `PLL0_EN=1`. None
released the measured hashing load. `PLL0_EN=0` did release load, but holding
all PLLs off at the normal voltage caused an unsafe midpoint shift.

The local abridged register map calls bit 1 `pll_clk_on` and describes it as
PLL clock output enable. The mining-process application note instead says to
keep bit 1 at its default (1). Neither description establishes a functioning
synchronized hash-clock gate on this silicon. Readback alone is insufficient.

## Method

- Four ASICs, nominal 960 mV per PCB half-stack, initial 100 MHz.
- Normal PLL/work requalification at 100 MHz before the intervention.
- One new ~4.0265-second SPDLOG window armed before all four probe stages.
- No WORK_CFG, SPD, FIFO-reset, or payload writes during the stages.
- Control: read counters, wait 100 ms, read counters again at EN=3.
- Live gate: broadcast EN=1, then the same two counter snapshots.
- Restart: broadcast EN=0; burst-write CFG for 125 MHz plus EN=1; verify
  every ASIC reads enabled+locked with `clk_on=0`; take two snapshots.
- Release: one broadcast EN=3; take two snapshots.
- Snapshots read CFG/EN and global pass/fail counters on each ASIC. Each
  counter timestamp is the midpoint of its host-side UART transaction.
  Intervals include snapshot overhead and are calculated individually.
- Log event timestamps after the critical sequence to avoid stretching it.
- Power off the ASIC rail after the attempt; no PLL rollback or auto retry.
- Independent 20 ms sampled rail guard retains the 1.20 V cutoff. It cannot
  bound shorter excursions; the scope capture is needed for that.

## Baseline limitation and attempts

Two preliminary attempts ended before any gating intervention because 960 mV
produced many hash errors at 100 MHz. The first retained work started at
800 mV; normal 100 MHz PLL/work requalification in the second attempt did
not cure the errors. Their full logs and exact images remain in
`baseline-rejected/` and `requalified-baseline-rejected/` (large artifacts
ignored by Git). No transition capture was produced by either attempt.

For the actual clock probe, the precondition was changed explicitly to
continued core activity on all four ASICs (pass+fail rate >=90% of nominal,
nonzero passing work, balanced PCB halves) while retaining the voltage limit.
This allows a binary check of whether clocks are withheld, without raising
voltage or claiming healthy mining. Advancing counters under EN=1 can disprove
complete clock gating; the experiment cannot establish a production sequence
or explain the low-voltage hash errors.

## Scope acquisition

Rigol DS2302A at 192.168.1.250; CH1=VDD−GND, CH2=GND0−GND. Both DC coupled,
1× probe factors, 20 MHz bandwidth limit. Upper half is calculated from the
matched raw CH1−CH2 samples. No calibration/offset correction was applied.

A host-triggered single acquisition spans 1.4 seconds at 20 MSa/s (28 million
points/channel, 200 ms pretrigger). Trigger follows the serial TRIAL marker;
firmware call timestamps are not direct measurements of clock-pin timing.
The full record includes the intentional power shutdown. Saturated samples
are excluded from calculated voltage extrema and plots.

`capture_probe.py` configures/captures the scope and records serial; it does
not flash firmware. `analyze_probe.py` decodes the saved raw preambles and
computes pass/fail counter rates. Experiment code is disabled in normal
firmware with `PROTO_RUN_CLOCK_DELIVERY_PROBE=0`.

## Measured voltage behavior

The raw 28-million-point records match in time (50 ns/sample). In this
low-voltage probe the measured lower/upper peaks were approximately
**1.072/1.064 V**, below the 1.20 V limit. Total VDD peaked at 2.008 V;
pretrigger medians were 1.944 V total and approximately 0.968 V per half.
The sampled firmware guard reported maxima of 957/968 mV, illustrating why
its readings cannot bound the brief transients.

The scope record contains a short disturbance during the complete PLL
stop/restart and deliberate ASIC power-off near 0.884 s after the trigger.
There is no external clock-pin measurement. These bounds apply to this
100→125 MHz, 960 mV experiment; they cannot be extrapolated to 425 MHz at
1160 mV. See `scope-summary.json`, `clock-delivery.png` / `.pdf`, and
`transition-raw.npz`. CH1 saturates low after shutdown; derived upper-half
voltage is omitted whenever either channel is saturated.

## Restoration and checks

The normal image was app-flashed with the probe disabled; NVS frequency and
voltage settings were not changed. Startup qualification passed at 400 MHz:
1979.48 GH/s total, 99.13% throughput, minimum ASIC 99.04%, PCB halves
1154/1165 mV. Subsequent mining was approximately 1.98 TH/s.

Both probe-enabled and normal firmware builds passed without compiler warnings.
The passive snapshot analysis checks monotonic counters/timestamps. Image
SHA-256 values are recorded in `firmware-sha256.json`; local binary images
and complete serial/build logs are intentionally ignored by Git.

## What to establish before another coordinated-start implementation

1. Does `PLL_EN[1]` control any clock path used by active hash cores on this
   MC3 revision, or only another output/test path? Is a separate write-enable
   or transfer operation required?
2. What exact, supported register sequence withholds all active hash clocks
   while leaving the PLL enabled and allowing lock detection? How is the
   gate released across every ASIC?
3. Why do the documented per-core/global SPD gate and PLL-selector updates
   change readback without changing the active hashing behavior in the prior
   tests? Is another handshake, operating mode, or silicon erratum involved?
4. If there is no usable gate, is there a supported method to change frequency
   or switch to a prelocked PLL without stopping the active hashing clock?
