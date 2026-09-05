# 1102 older-firmware versus current startup review

2026-09-05. Read-only firmware investigation; no device reset, flash or live bench test in this review. User reports this exact 1102 hardware has repeatedly achieved 600 MHz / 3 TH/s. That establishes a demonstrated operating point and makes differences in initialization, PLL programming and work sequencing a priority.

**New concrete finding: the current PLL divider table is different at every 25 MHz point from 450 through 600 MHz.** The complete 37-entry historical table was found byte-for-byte in the saved old firmware image, not just inferred from its version string. The current bench is therefore not exercising the old PLL configuration even when it logs the same MHz.

## Evidence identity and limits

- Actual older image: `../2026-09-04-1102-warm-ramp/previous-factory.bin`, a 4 MiB factory partition backup taken from this board before flashing. SHA256 `01551029f035275096cb56e7b2b298335ef43535f25b447a4b3ea9f347fbe802`.
- User's successful warm boot log: `../2026-09-04-1102-warm-log/original.log`, version `v2.14.0-40-gcd82a9e-dirty`, compiled July 24, ELF SHA prefix `3186ecc06`. First healthy sample: 2949.64 GH/s. The device was reset without removing input power.
- Reference source: `cd82a9e43e59d6fa92c7bd13ca14c9d5cabc2e43`; this is NOT the exact dirty source. The successful log and backup contain the 800 mV/100 MHz voltage ramp and 31.75 ms soft start absent from that clean revision. Those paths resemble the later `9df1d31` implementation. Do not rebuild clean `cd82a9e` and label it an exact reproduction.
- Current source: `be978c3` plus working modifications, including the disabled first-zero hold mode. Exact review snapshots are `current-mc3.c` and `current-asic_init.c`.
- Latest actual bench trace: `../2026-09-04-1102-zero-hold/serial.log`. A1 D1 and A2 D2 failed at 475 MHz and stayed at zero PASS for 120.930 seconds.

## 1. PLL divider values changed at the failure-region frequencies

The backup contains all 296 bytes of the historical 37-entry table at **factory-relative offset `0x36F18`**. Entries use the historical 8-byte structure layout `<HBxHBB` (MHz, refdiv, padding, fbdiv, postdiv1, postdiv2). See `old-binary-pll-table.json`. This verifies the table stored in the installed dirty artifact; it is not a bus trace proving each register write.

REFDIV is 1 in every row below. Register words use the unchanged packing function, consistent with the local MC3 errata. Post-divider values are raw fields, not stated physical divisors.

| MHz | Old FBDIV / POSTDIV1 / POSTDIV2 | Current fields | Old PLL0_CFG | Current PLL0_CFG |
|---:|---|---|---|---|
| 450 | 144 / 7 / 0 | 144 / 3 / 1 | `0x01C10908` | `0x02C10908` |
| 475 | 152 / 7 / 0 | 152 / 3 / 1 | `0x01C10988` | `0x02C10988` |
| 500 | 160 / 7 / 0 | 160 / 3 / 1 | `0x01C10A08` | `0x02C10A08` |
| 525 | 147 / 6 / 0 | 126 / 2 / 1 | `0x01810938` | `0x028107E8` |
| 550 | 154 / 6 / 0 | 132 / 2 / 1 | `0x018109A8` | `0x02810848` |
| 575 | 138 / 5 / 0 | 138 / 2 / 1 | `0x014108A8` | `0x028108A8` |
| 600 | 144 / 5 / 0 | 144 / 2 / 1 | `0x01410908` | `0x02810908` |

All other 30 historical table entries match current tuples. The seven changes and 49 fractional entries arrived in `be978c3` (domain-balancing diagnostics). Earlier notes called an alternate tuple “equally valid”; that means a nominal frequency alternative, not demonstrated equivalence of clock behavior, load transients or startup reliability. At 525/550 MHz even FBDIV changes. The first changed table point is 450 MHz, immediately below the repeatable 475 MHz failure in the recent 1102 runs. This correlation is a reason to test, not proof of causation. The earlier 1103 failure at 425 MHz cannot be explained solely by changed table entries, since 425's tuple is unchanged.

Current source locations: `components/asic/mc3.c:198` (table), `:438` (packing), `:1986` (broadcast frequency function).

## 2. Test work and reset strobes now precede and follow every frequency change

The successful old log shows:

1. Start at 800 mV/half and 100 MHz, ramp voltage to 1160 mV/half; completed at 19.170 s.
2. Broadcast PLL ramp in 25 MHz steps with 500 ms waits through 500 MHz and 1000 ms above it.
3. 600 MHz lock at 30.681 s; ramp completes at 31.681 s.
4. First pool job logged at 32.221 s; mining configuration at 32.225 s, **1544 ms after final PLL lock**.

The reference frequency-ramp function has no qualification work or SPD logger arm at every step. The actual backup contains the voltage-ramp log strings but none of the later qualification-specific strings. These observations support a different work-start sequence; absence of pool work does NOT prove that clocks were gated or that there was no internal switching during ramp.

Current bench startup explicitly starts synthetic qualification work at 100 MHz **before raising Vcore**, then qualifies each 25 MHz step through 300 MHz. After warmup, each higher step calls `MC3_start_broadcast_recovery_probe`:

```
WORK_CFG = 0x8800D2FF             apply/reset before PLL update
wait 10 ms
broadcast PLL transition and lock check
mc3_program_qualification_work   GLOBAL_SPD, ROLLTIME, WORK_CFG reset/mining,
                                V_CTRL, target, version bases, job registers
arm SPDLOG                      GLOBAL_SPD, WORK_CFG SPD/FIFO actions,
                                counter reset and timer
observe hashing                 ~20 seconds per intermediate frequency
```

At the failing 475 MHz launch, lock was logged at 573.700 s, mining configuration at **573.701 s**, and SPD observation arm at 573.851 s. Thus the work configuration starts about **1 ms after lock**, despite the much longer total per-step dwell. Waiting longer AFTER a bad launch is different from delaying work BEFORE it starts.

The job also differs: the old pool job used version mask `0x1FFFE000` / V_CTRL `0xFFFF00D1`; the fixed qualification job uses `0xFFFF0000` / `0xFFFF0101`, a synthetic header and fixed target. This is another variable; the old job and new synthetic work are not identical workloads.

`0ac9d30` (qualify MC3 operating point before mining, September 2) introduced early qualification work and the pre-PLL reset / immediate work-reapply sequence. The current warm harness extends it. Source: `main/power/asic_init.c:3350`, `components/asic/mc3.c:1099`, `:2438`, `:2562`, `:2777`.

## 3. PLL CFG and EN=1 were merged into a burst write

Old source sequence:

```
GLOBAL_SPD → EN=0 → CFG → EN=1 → EN=3 → ROLLTIME → lock checks
```

Current broadcast sequence:

```
GLOBAL_SPD → EN=0 → [CFG, EN=1 in one packet] → EN=3 → ROLLTIME → lock checks
```

This change is also in `be978c3`. It intentionally removes one command/response interval while the PLL is stopped; prior scope notes report approximately 4.44 ms versus 2.72 ms. The nominal register order is preserved, but spacing and packet boundaries change. It should be compared separately from the divider table and work timing. Proto's local errata lists separate CFG, EN=1, EN=3 steps; it does not specify a minimum settling delay or validate a burst as electrically equivalent.

## 4. Voltage and regulator-enable behavior differ

- Successful old boot: **1160 mV per half**, versus temporary **1140 mV** in recent bench runs. A controlled software comparison must account for this 20 mV/half difference.
- Both actual old log and new 1102 setup use **1300 kHz**, scale **0.125**, compensation **13 20 C6 19 C6**, and **31.75 ms** TON_RISE. The clean old commit does not capture all these startup details; actual logs are the stronger reference.
- Old final ON_OFF_CONFIG was **0x1B** (a second config write overwrote the initial 0x1F). Current verified config is **0x1F**, with GPIO10 actively controlling TPS enable. The old 1102 board entry did not enable GPIO control. This affects power sequencing, not just telemetry; retain it as a secondary variable rather than casually reverting independent shutdown control.

## 5. Calling the old ramp function name would not restore the old ramp

`MC3_ramp_hash_frequency()` walks every entry of `PLL_CONFIGS`. The current table contains **57 transitions from 400 to 600 MHz**, versus **8** in the old table, owing to the 49 added fractional entries. The latest bench uses explicit +25 MHz steps and therefore did not visit those fractional entries. A reproduction must use the historical frequency list explicitly or restore the old table, not simply call the existing generic ramp and assume it is unchanged.

## Recommended comparison order

1. **Smallest targeted test:** restore only the seven historical divider tuples in an isolated bench build, retaining the same 1140 mV, 300 MHz warmup, +25 MHz ramp, report-only voltage policy and first-zero hold. This directly tests the newly verified table difference against the last run. Do not also change PLL packet timing or work resets in this comparison.
2. **Historical positive control:** use the saved exact old image with its 1160 mV setting and the user's successful warm-reset procedure, while recording the scope and actual throughput. Capture CH1 at 50 Ω. A clean `cd82a9e` rebuild is not this control. Success establishes a current baseline but does not isolate which software difference matters.
3. With historical tuples fixed, compare separate CFG/EN packets and then work timing: remove qualification-specific resets/work reapplication during the final ramp, use the historical 25 MHz step list and 500/1000 ms waits, and apply work after final settling. Match voltage and measured starting temperature. A 300 MHz hashing warmup requires a deliberate reset/reinitialization step if the following ramp is to reproduce the old pre-work startup state.
4. Only if those controls fail, compare remaining differences such as GPIO/PMBus power sequencing, PVT conversion traffic and synthetic versus real pool work. Sensor reads are not completely passive writes: conversions still program PVT control registers.

No root cause is proven by this review. The prior no-recovery result means a two-minute wait did not repair that particular startup state; it does not contradict this board's repeated historical 600 MHz success.

## Reproducibility

`compare_sources.py` snapshots old/current source, compares all divider tuples, searches the exact backup for qualification-related strings and extracts boot evidence. `old-binary-pll-table.json` records the full-table byte match. `pll-table-comparison.json`, `old-boot-evidence.txt`, `current-boot-evidence.txt`, and `comparison.log` contain supporting data. Source investigation only; no firmware source changed during this review.
