# Full boosted startup: warm at 1160, ramp at 1200, mine at 1160 mV — 2026-09-08

User-requested full mining test on stock 1103:

1. Save operating voltage **1160 mV/half** and frequency **600 MHz**. Initial
   voltage ramp and 100–300 MHz warmup use 1160 mV. Every ASIC must reach 40 C
   for the established ten-second dwell.
2. Retain the validated full Vcore/VDDIO cycle and warm reinitialization at
   100 MHz / 800 mV. Raise voltage in 10 mV / 150 ms steps to **1200 mV**, then
   complete the unloaded PLL ramp to 600 MHz (2 s per 25 MHz above 300, final
   settle 1.5 s).
3. With PLLs unchanged at 600 MHz, command **1160 mV**, settle 1 s and confirm
   fresh total regulator voltage in 2270–2370 mV. Reinitialize only PVT, wait
   100 ms, and require valid fresh temperatures >=40 C before first work.
4. Send the existing first qualification job, check chip/domain throughput,
   then hand off to pool mining if successful. Keep mining at saved 1160 mV.

The prior deliberate diagnostic stop and paired before/after PVT sweeps are
removed from this path. Normal temperature/fault/throughput guards remain;
invalid pre-work temperatures still retain the failure-only voltage diagnostic.
The new boost is limited to 1103 at saved 1160 mV / target 600 MHz; 1102 and
other operating points retain the unboosted sequence. No change to MC3 first
work, PLL command sequence, TPS546 compensation or established PVT setup.

Host tests cover exact voltage/PLL/PVT/job order, no jobs during ramp, weak-core
acceptance, domain collapse, voltage drop/rail/reinit/pause failure and shutdown,
and unchanged 1102 behavior. PVT-only command tests pass. ESP-IDF 5.5.1 firmware build and `git diff --check` passed.

## Hardware procedure

Cool ASICs with power off and fan confirmed 100% for at least two minutes,
restore original automatic fan control, save 1160 mV and flash application-only
OTA. Read fresh startup temperatures via HTTP. No serial or scope is used.
If qualification passes, observe five minutes of pool mining and calculate
hashrate from complete per-core counter windows. Report all sixteen domains
separately from all 624 cores; prior successes had one persistent zero-PASS
A1 D2 core 105. Estimated J/TH uses TPS546 output power (Proto allowance 0 W),
not wall power. A failed startup stays powered off without automatic retry.

## Result: PVT thermal recovery succeeds; mining qualification fails

Application-only OTA succeeded. Application SHA256:
`fb3307320ecef993a781865cb69ad86a3e57fde51e3c0477b3d5c96a47d5e928`.

- First fresh temperatures: **27.79 / 27.13 / 26.80 / 27.46 C**.
- Final warm 300 MHz qualification: **1486.28 GH/s**, all sixteen domains
  active; the established A1 D2 core 105 remained zero-PASS.
- Full supply cycle discharged to **34 mV**, then initialized all four ASICs.
  No work was sent during the 1200 mV ramp to 600 MHz.
- After lowering to 1160 mV and settling one second, fresh TPS546 total was
  **2323 mV**. PVT-only setup took **138 ms**; all four read back sensor
  STATUS=0, CLOCK=0x01010000 and PLL0_EN=0x7. PLLs were unchanged.
- First fresh post-reinit temperatures at 600 MHz were
  **47.58 / 46.10 / 46.92 / 46.76 C**. All four remained valid throughout
  qualification, reaching **49.40 / 48.08 / 49.73 / 49.07 C**. This full-run
  path did not perform the prior separate 12-round voltage-conversion sweep;
  voltage setup/status readback alone is not proof of fresh VDD conversions.
- First work was sent at **1160 mV**, after PVT reinitialization. The final
  qualification logged **2045.08 GH/s, FAIL**, with **429/624 active cores**
  across **11/16 active domains**:

| ASIC | Logged GH/s | Domains with zero PASS counts |
|---|---:|---|
| A1 | 373.42 | D0, D2 |
| A2 | 371.34 | D0, D2 |
| A3 | 557.79 | D2 |
| A4 | 742.52 | None |

Every failed domain had all 39 cores at zero PASS. Surviving domains measured
approximately 185–187 GH/s from the cached complete counters. The cache-derived
total is 2045.13 GH/s; its window durations differ slightly from the firmware's
logged calculation. `qualification-summary.json` records those cached values.
`domain-summary.json` and `core-efficiency-summary.json` describe pool windows
only; there were **none**, so their zero-domain count is not a healthy result.

The largest sampled half-stack voltage was **1209 mV**, with five readings
above 1200 mV. These are report-only ADC samples, not scope transient captures.
Shutdown was due to failed throughput qualification, not a temperature or
sampled voltage abort. No pool work or five-minute efficiency measurement ran.
The final API read confirmed ASIC power off (**32 mV** regulator total), saved
1160 mV / 600 MHz and restored automatic fan control (99%). Temperatures and
actualFrequency in the powered-off API state are retained values. No retry.

This run confirms valid thermal sensing after PVT reinitialization at 1160 mV,
but does not establish successful high-voltage mining before the drop: the
first 600 MHz job was sent only afterward. The failure could have developed
during initialization/ramp, the voltage drop, PVT setup, or first-work startup
at 1160 mV. The comparison needed to test voltage reduction after stabilization
must first establish healthy work at 1200 mV, with valid temperature monitoring,
then lower voltage while retaining the working PLL/job state.
