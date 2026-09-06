# Passive first-launch / delayed identical-work comparison

Stock 1103, MAC 28:84:85:BA:96:08; no scope probes. Branch `1103x-support`.

Question: after the usual 300 MHz thermal warmup, warm supply cycle, reinitialization,
and unloaded ramp to 600 MHz, does a weak internal domain recover while left alone,
or after one delayed identical-work/configuration reset with no PLL writes?

## Protocol

- Preserve stock TPS546 profile, 1160 mV per half and production warmup criteria.
- Preserve full Vcore/VDDIO cycle, reset/reinitialize at 100 MHz, unloaded broadcast
  ramp to 600 MHz, and final 1500 ms settling period.
- Before first payload: normal work/SPD setup (omitting the redundant PLL EN write),
  select the qualification counter target with a non-strobing WORK_CFG value, and
  reset/enable only SPDLOG_RST/TIMER for a single 120.04-second counter epoch.
- Submit synthetic qualification job 0. Read per-core counters, grouped into four
  internal domains per ASIC, for at least 10 seconds. No work/SPD/PLL configuration
  writes occur during the hold; temperature sensor conversions continue.
- Repeat identical work/configuration and job 0 once, with no PLL register writes
  and no SPDLOG_RST/TIMER writes. Observe another 30 seconds, then power ASICs off.
- Verify PLL CFG/EN readbacks and software write counts at phase boundaries and
  throughout each hold. Both single and paired register-write transports are audited.
- Retain fresh-temperature checks (<70 C), rail/read/cancellation guard and bounded
  startup. Do not reject a domain merely for zero PASS during this recovery probe.
- Finish paused with ASIC rail off and manual fan 100% for cooldown.

This is an instrumented launch, not a bit-for-bit repetition of earlier production
launches. It selects the usual qualification counter target (24 leading zeros)
before the first job, instead of reapplying SPD/FIFO strobes after that job.
The repeated reset is a combined identical-work/configuration intervention; it does
not isolate FIFO reset, SPD apply, or job submission individually.

The public bench API is inert unless explicitly called; the startup probe's
compile-time flag is disabled in the normal source after capturing the test build.
A completed probe deliberately bypasses pool handoff and takes the power-off path;
its `PASSIVE END completed=1` marker distinguishes completion from failure even
though the existing startup cleanup also logs `PROTO STARTUP FAILED`.

## Validation

`test_passive_launch.py` compiles the real configuration/launch/audit functions
against a recording transport. It checks identical reset writes/payload, counter
arm ordering, zero PLL writes, passive readback, write-failure propagation, and
600 MHz preflight rejection. Existing startup, handoff, producer-resume, and
recovery-task host tests also pass. ESP-IDF 5.5.1 application build passed.

`run_probe.py` captures UART and whitelisted API fields, checks board identity,
enforces a 900-second host deadline, and pauses/cools on exit. `analyze.py` derives
per-domain rates from counter growth and reports any decrease within a hold;
it does not use cached API hashrate to infer recovery.

## Results

Completed one cold-start trial (`run-01`). First fresh readings at 100 MHz were
28.61/28.12/27.95/28.45 C. Warm 300 MHz qualification passed at 1480.79 GH/s.
The supply cycle discharged to 36 mV and reinitialized all four ASICs.
Before the first 600 MHz job, temperatures were 56.00/54.51/55.67/55.17 C.
Maximum fresh 600 MHz temperature during the comparison was 57.15 C.

The first job/configuration call followed the final PLL-step log by 1.654 seconds
(including thermal and PLL readbacks). The identical-work reset was called
10.275852 seconds after that first call. Linear fits to passive domain PASS
counter growth give:

| ASIC | Before reset, GH/s | After reset, GH/s |
|---|---:|---:|
| A1 | 737.48 | 737.10 |
| A2 | 738.86 | 740.80 |
| A3 | 741.07 | 741.82 |
| A4 | 744.07 | 742.93 |
| Total | **2961.49** | **2962.65** |

The holds ran at least 10 and 30 seconds; usable first-to-last counter sample
spans were 9.42 and 29.64 seconds. All 16 internal domains were active in both
periods. A1 D2 retained its previously seen single zero-PASS core, producing
96.51% and 96.03% of ideal domain throughput. Every other domain produced
98.48–99.88% before and 98.53–99.46% afterward. There were no whole-domain
zero-PASS failures and no counter decreases within either hold.

Audit: PLL writes stayed at 134; all four PLL CFG/EN readbacks remained
`01410908` / `00000007`. Logger writes increased 20 -> 22 only before the
first job, then stayed at 22. Other audited writes were 622 throughout the
first hold and 657 throughout the second hold: exactly 35 writes for the
identical-work/configuration reset and zero additional writes during either
hold. No write failures were reported.

**This run did not reproduce a failed domain, so it does not demonstrate recovery
of one.** It establishes that this instrumented launch can start balanced from
cold after warmup, and one delayed identical reset can preserve that balance.
The previous post-job counter-arm helper is a candidate for a controlled add-back
comparison, but this single run does not implicate it: the omitted redundant PLL
EN write and altered measurement timing also differ from earlier failed runs.
No scope was used, so these data make no claim about voltage transients.

Installed probe image: application SHA256
`448cf6c008f28b3be54d089f4e7e975e14ed46bd0b4bacdec1ebc9b7aee1884f`,
ELF SHA256 `50889078a3fa33ca59161034f38c689be1240b9c005268f41269e09a5ca0f64a`.
The complete probe source snapshot is beside the raw log. Normal source differs
only by disabling `PROTO_RUN_PASSIVE_LAUNCH_PROBE`; its successful rebuilt image
was **not flashed**. The installed probe will run again on restart/resume.

Final state verified: paused, ASIC rail 36 mV, manual fan 100%, saved
600 MHz / 1160 mV retained. Logged temperatures after power-off are cached and
must not be treated as fresh cold-ASIC readings. This was synthetic qualification
work, not a pool-mining endurance run.

Raw logs, binaries, detailed analysis and source snapshots stay local;
`run-01/summary.json`, `result.json`, and `firmware-identity.json` preserve the
curated measurements and image identity.
