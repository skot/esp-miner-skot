# Three additional 1200 mV startup observations — 2026-09-07

User requested more 1200 mV restarts after A3/A4 conversion failure on the
first 1200 mV test. Keep identical installed firmware: every ASIC >=40 C,
ten-second warmup dwell at 300 MHz, 2-second high-frequency PLL steps,
1.5-second final settle, 1200 mV/half. No firmware, work or guard changes.

The first API inspection found a new boot already underway at uptime 35 s,
150 MHz, with fresh temperatures around 27–28 C. Observe it without interruption
as attempt 01; its restart trigger and prior cooling procedure were not
controlled by this run. Attempts 02 and 03 use API restart after at least two
minutes of ASIC power off with fan confirmed 100%, restoring original automatic
fan control before restart. If the preceding run is mining, use pause and the
existing pre-power range validation (temporarily saved 1201 mV) to permit fan
100% while ASICs remain off; never apply that rejected setting to powered ASICs.
If already off after failure, simply select manual fan 100%. No serial or scope.

Each startup success receives a five-minute pool hold before the next restart.
Classify thermal abort before work separately from measured domain failure.
Final successful attempt stays mining; final failure remains powered off.
## Results: all three additional boots aborted before first work

No firmware was rebuilt or flashed, and the startup, MC3 and vcore source
hashes stayed identical throughout this batch. The known installed image is
`f77ac103893b7213393299e0b77398c3fd55cd970f5ce91888296c0bc2f96f9a`.

| Attempt | Fresh initial temperatures | Warm 300 MHz qualification | Post-ramp result | Sampled peak half-stack |
| --- | --- | --- | --- | --- |
| 01: already underway when observed | 26.64–27.29 C | 1487.16 GH/s PASS | A3/A4 conversion abort | 1214 mV |
| 02: cooled API restart | 27.13–28.12 C | 1490.02 GH/s PASS | A3/A4 conversion abort | 1217 mV |
| 03: cooled API restart | 26.31–26.96 C | 1487.45 GH/s PASS | A3/A4 conversion abort | 1216 mV |

Every attempt had valid initial/warmup temperatures and passed warm 300 MHz
qualification. After the supply cycle and unloaded 600 MHz ramp, **A3 and A4
failed all three conversion attempts** with the same `done=0`, `status=3`,
`raw=0`, valid transport reads and decoded invalid -288.90 C. A1/A2 remained
valid: final retry temperatures were 48.24 / 46.76 C, 46.59 / 45.27 C and
47.91 / 46.43 C respectively. Older A3/A4 temperatures shown in the API after
failure are retained warmup values, not valid fresh 600 MHz measurements.

No attempt sent first work at 600 MHz, so none has a current 600 MHz core/domain
throughput measurement, five-minute mining hold, accepted shares or sustained
J/TH result. Diagnostic epoch 0 after failure must not be interpreted as zero
active cores. Each attempt's `result.json` records these values as unavailable.
`startup-events.json` preserves qualification and thermal failure evidence.

Including the original test in `../2026-09-07-1103-1200mv-40c-2s`, **four of
four observed 1200 mV / 40 C / 2-second-step startups** showed the same A3/A4
pre-work conversion failure. This is a repeatable symptom under this tested
configuration. Both immediately preceding 1180 mV / 40 C comparisons obtained
valid 600 MHz temperatures but lost domains under work. However, a prior hot
1180 mV attempt also showed this conversion failure and then passed after an
identical restart. The observations strengthen a voltage-associated startup
hypothesis without isolating direct sensor voltage sensitivity from PLL/reset
or conversion sequencing. They do not establish that higher voltage itself
makes the sensor hardware fail.

The final API check at uptime 245 s confirmed **ASIC power off (32 mV)**,
startup `failed`, diagnostic epoch 0, zero shares, saved 1200 mV / 600 MHz,
and original automatic fan control (99% output). The firmware remains at
40 C / 2-second steps; no automatic recovery is scheduled. No scope was used,
so voltage maxima above are sampled values rather than transient bounds.
