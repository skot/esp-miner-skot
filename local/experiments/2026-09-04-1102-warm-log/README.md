# User-supplied warm 1102x restart log

The user reports that 1102x can start imbalanced from ambient cold, then reach
600 MHz / approximately 3 TH/s after warming and restarting. This attachment
contains the end of a failing run and the start of one successful warm run;
it does not include the earlier cold-start sequence or a long healthy hold.
The user confirmed a reset-button/software restart, without removing input
power. The log reads regulator OPERATION=0 and VOUT=0.05 V before ASIC startup,
consistent with cycling the ASIC rail while the board remains warm. This is
not a controlled full input-power cycle or evidence that every internal rail
fully discharged for a specified time.
No hardware operation or firmware change was made for this comparison.

## What the log shows

| Observation | Before restart | After restart |
| --- | --- | --- |
| Reported temperature near counter sample | 58.3°C | 52.4°C |
| A1 passing throughput | 372.92 GH/s | 740.58 GH/s |
| A2 passing throughput | 366.62 GH/s | 730.10 GH/s |
| A3 passing throughput | 744.04 GH/s | 739.08 GH/s |
| A4 passing throughput | 745.30 GH/s | 739.88 GH/s |
| Total passing throughput | 2228.88 GH/s | 2949.64 GH/s |
| A1 / A2 FAIL fraction | 7.51% / 15.60% | 0.92% / 1.25% |

At the stated 600 MHz target, ideal throughput is 748.8 GH/s per ASIC.
Thus the pre-restart A1/A2 result is approximately half throughput, not the
three-quarter throughput associated with D2 alone. There are no internal
domain counters here to identify which domains were affected. FAIL counts
show hashing errors as well, but they do not account for all the lost passing
throughput. The pre-restart frequency is inferred from the user report and
A3/A4 throughput; the clipped pre-restart section has no PLL setting line.

After restart, firmware reports board 1102 and
`v2.14.0-40-gcd82a9e-dirty`. A follow-up boot header gives compile time
Jul 24 2026 11:59:35, ELF SHA256 prefix `3186ecc06...`, and ESP-IDF
`v5.5.1-dirty`. The host monitor reports its locally built application checksum
as `5bba4c53c1fe0655e8306b53d75d7a202da9f9e01f3a76e677d53f3bc53fd702`,
which differs from the flashed application. This is not evidence by itself
that the flashed image is corrupt; it prevents treating the host build as
the exact running artifact. Its `cold boot mode` message names the firmware
initialization path, not the physical temperature. Temperature is 51.4°C when
fan PID starts, and 52.4°C at the first healthy SPDLOG sample. Vcore is set to
the same 1160 mV per half-stack / 2.32 V total used in the 1103x reference.

At the end of the 100 MHz voltage ramp, ASIC VDD telemetry is
1149.8, 1150.3, 1174.1, 1177.4 mV. These are per-chip VDD readings, not D0–D3
voltages. No post-ramp PCB midpoint or 600 MHz voltage measurements appear,
so this log establishes near-balanced chip throughput, not verified internal
voltage balance or absence of fast overvoltage excursions.

## Startup sequence comparison

- At 19.170 s, the 800→1160 mV ramp completes at 100 MHz.
- Frequency advances in 25 MHz steps, waiting approximately 500 ms after
  lock through 500 MHz and 1000 ms at 525 MHz and above.
- At 30.681 s, all four PLLs report locked at 600 MHz.
- At 31.681 s, the ramp returns successfully.
- At 32.221 s, the first pool-work write is logged; mining configuration is
  logged at 32.225 s, about 1.544 seconds after the final lock report.
- At 35.754 s, the first recorded healthy SPDLOG sample totals 2.950 TH/s.

The local reference commit `cd82a9e43e59d6fa92c7bd13ca14c9d5cabc2e43`
contains broadcast GLOBAL_SPD, EN=0, CFG, EN=1, EN=3, ROLLTIME writes followed
by lock checks. Its frequency-ramp loop has the delays above and does not
reapply qualification work or arm a qualification counter window at each
step. The later pool-work path writes WORK_CFG apply/reset and mining
configuration. The running firmware is marked **dirty**, so the reference
commit cannot establish its exact source or every unlogged register write.
In particular, absence of pool work during the ramp does not prove absence
of hashing or effective clock gating.

Our 1103x diagnostic instead reapplies qualification work and arms SPDLOG
at each frequency step, then waits approximately four seconds to qualify.
Therefore board revision, temperature, firmware, work timing, and reset
history are confounded in a direct comparison.

## Implication for the investigation

The board was failing while hotter than it was during the successful restart.
Combined with the user's cold/warm observation, this favors a startup state
whose outcome depends on temperature and history, rather than a simple rule
that exceeding a temperature threshold restores throughput. It does not
prove the underlying physical mechanism. A1/A2 susceptibility also predates
1103x, weakening an explanation specific only to the 1103x PCB changes.

The 1103x recovery at approximately 64 seconds could involve thermal evolution;
that run did not capture temperatures, so elapsed time must not be promoted
to a PLL-settling constant. The next controlled comparison should keep board,
firmware, voltage, PLL sequence and final-work delay fixed, vary measured
temperature at the transition, and record PASS and FAIL per domain, internal
PVT, PCB voltages and temperature together. Comparing passive waiting with
reinitialization at a matched temperature would separate warming from the
effect of the reset. Existing voltage limits still apply.

The original attachment is retained locally as ignored `original.log`;
`serial-evidence.txt` contains relevant excerpts, and `summary.json` contains
the source hash and calculated rates. No new bench trial was run.
