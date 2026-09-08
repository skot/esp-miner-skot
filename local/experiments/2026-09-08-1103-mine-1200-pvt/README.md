# Hold mining at 1200 mV after PVT recovery - 2026-09-08

User-requested mining comparison with no voltage reduction after ramp/recovery.
Save1200 mV /600 MHz so normal power management keeps that target after startup.
For1103 at this operating point only, initial warmup voltage is1160 mV. Warm
at300 MHz until all four ASICs are>=40 C for ten seconds, then use the established
full Vcore/VDDIO cycle,800 mV/100 MHz reinit, ramp voltage to1200 mV and ramp
unloaded to600 MHz with2 s high-frequency steps and1.5 s final settle.

Retain the successful sensor comparison before first work: wait1 s, check fresh
TPS total2340-2460 mV, read before temperatures/voltage conversions, reapply the
same PVT-only setup, wait100 ms and read fresh temperatures/voltage again.
Invalid pre-reinit readings may proceed through recovery; credible hot readings,
guard trips and invalid post-reinit temperatures abort. MC3 source/setup/PLL/
first-work commands are unchanged. Retain the paired sweeps to match the prior
successful recovery timing. Their fresh per-chip result is logged; the diagnostic
voltage helper itself returns void, so the firmware gate requires temperatures,
while the host assesses fresh voltage conversion results from logs.

Then check fresh temperature>=40 C, send first work, qualify600 MHz core/domain
throughput, and hand off to normal pool mining only on success. Retain1200 mV
throughout. Normal fault/temperature/throughput protections remain. Observe five
minutes of pool operation if successful, using complete per-core counter epochs,
all four PVT temperatures/chip voltages and pool shares. No scope or serial.
ASIC-off/fan100 cooling precedes OTA; restore original automatic fan control.
Failure leaves ASICs off without retry; success leaves the board mining1200/600.

Host tests passed for warmup voltage selection, fixed1200 recovery/job/handoff,
weak-core acceptance, cold/invalid/hot temperatures, failed recovery, domain
collapse, voltage/PLL/chain/pause failures and other operating points. Exact
sensor-only command tests passed. ESP-IDF 5.5.1 build and `git diff --check` passed; application-only OTA succeeded.

## Result: healthy domain hashing and PVT maintained at 1200 mV

Application SHA256:
`216b1d8c616570d7fa2f38c742a77a5190f3f2f0282ab8affe75a94fa2c9f643`.
Saved settings1200 mV/600 MHz; measured warmup rail2323 mV total confirms
1160 mV/half during warmup. Initial fresh temperatures26.47-27.29 C.

After the unloaded ramp, fresh regulator total was2401 mV. A3/A4 again had
invalid temperatures and no completed voltage conversions before PVT setup.
After the same PVT-only reinitialization, all four temperatures were valid
(**48.90/47.42/48.08/48.24 C**) and all four completed12/12 fresh voltage
conversions. PLLs remained at600 MHz. No voltage reduction occurred.

First600 MHz work started at uptime196.505 s. Qualification passed at
**2964.41 GH/s** at208.872 s; all16 domains active,623/624 cores with PASS
counts. ASIC rates738.81/740.77/742.87/741.97 GH/s. Pool handoff followed.

Five-minute hold (303.86 s,48 API samples,42 complete distinct core-counter
epochs):

- Counter-derived mean **2969.55 GH/s**; range **2954.60-2984.44 GH/s**.
- **No zero-throughput domains** in any complete epoch. Minimum individual
  domain throughput93.35% of ideal across these short windows.
- Only logical core105 in A1 D2 was ever zero-PASS. It was zero in41/42
  epochs and produced0.02 GH/s in the other. Active-core counts technically
  range623-624, but this is not meaningful recovery of that weak core.
- Four positive temperatures and four positive chip-VDD readings in all48
  pool samples, with values changing on every ASIC over the hold; no logged
  post-handoff PVT conversion failures. Peak temperature **54.35 C**.
  Pool VDD readings spanned about1189.49-1200.06 mV across the four ASICs.
  These are chip VDD readings, not measurements of the PVT auxiliary supply.
- Saved1200 mV and actual600 MHz in every sample. Sampled half-stack peak
 1212 mV (report-only ADC; no scope). Measured total rail remained around2.40 V.
- Hold end: **40 accepted /1 rejected share**. The reject was explicitly
  `Difficulty too low` at269.725 s after a1000→16384 difficulty increase at
 269.271 s. No corresponding domain collapse was observed.
- Mean reported power **50.12 W**, approximately **16.88 J/TH** from regulator
  output power (Proto board allowance0 W), **not wall power**.

The raw API-rate mean in result.json includes the initial unpublished0 value;
use the complete-counter mean in domain-summary.json for performance. The
sensor-result and pvt-hold-summary files retain sensor recovery/hold evidence.
A final API check at uptime565 s confirmed continued mining at1200/600,
regulator2401 mV, temperatures51.87-53.36 C, and41 accepted/1 rejected share.
Automatic fan control was restored (99%). The board is left mining, with no
further voltage change or restart. This firmware now proceeds to pool mining
on successful recovery/qualification; the deliberate diagnostic stop is removed.

This establishes one cold-start success followed by five minutes of healthy
full-domain hashing and working PVT at1200 mV. It does not establish reboot
repeatability or long-term stability. Lower-voltage efficiency testing remains
for a later user-requested comparison.

## User-observed restart follow-up

Before the checkpoint commit, the user reported a couple of additional manual
restarts working well with this firmware. The agent did not capture their
reset types, exact number, or per-run telemetry. This adds encouraging manual
repeatability evidence alongside the instrumented cold-start/hold result above.
No further firmware or mining-voltage change was made for the checkpoint.
