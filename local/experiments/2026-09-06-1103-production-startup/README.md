# Production warm startup for manual boot testing

User request: remove restart/cooldown experiments, warm at 300 MHz, ramp to
600 MHz, and keep mining if the final operating point qualifies. Manual cold
and warm boot testing is left to the user.

## Firmware behavior

- Stock 1103 TPS546 configuration is unchanged. The attached board retains
  1160 mV per half and a 600 MHz target in NVS.
- Detect four ASICs at 100 MHz, raise voltage, qualify through 300 MHz, then
  use a temporary fan override to reach the established warm-start conditions.
- Preserve the one short warm Vcore/VDDIO cycle and reinitialization that was
  part of the successful sequence. This is an initialization step, not a
  repeated cold/warm test. No ESP restart occurs inside startup.
- Raise voltage again and broadcast the unloaded PLL ramp to 600 MHz, including
  the final 1500 ms settling interval; no jobs during that ramp.
- Configure the logger before the first job, with no redundant PLL EN write.
  Check passive per-core PASS/FAIL counter growth for ten seconds, checking
  fresh temperatures each second. Do not reset/reapply work during this check.
- Require >=97.5% ideal throughput and >=97% PASS rate per ASIC, with every
  internal domain contributing >=90% of its equal-share expectation.
- On success, clear synthetic host-side work state, hand over to ordinary pool
  mining and release the startup fan override. Continue at the saved frequency
  under normal temperature/fault protection. There is no timed shutdown,
  delayed identical-work reset, repeatability loop or cooldown cycle.
- On initialization/qualification failure, leave ASIC power off and report the
  failed condition instead of starting an experimental recovery sequence.

Removed the firmware's diagnostic selectors, frequency/voltage experiment
loops, addressed/mixed/alternate-PLL experiments, recovery probes, and write-audit
instrumentation. Renamed the retained fan lease for its actual startup purpose,
and restored ordinary 30% fan behavior while paused. Removed the recent host
restart/cold-resume/cooldown controllers. Historical results and analysis remain;
removed implementations are recoverable from Git.

## Validation

ESP-IDF 5.5.1 build and the following host checks pass:

- `test_first_work.py`: actual one-shot launch/configuration code, counter arm
  before payload, zero PLL writes, write failure propagation and profile checks.
- Updated production startup harness: ordering, unloaded ramp/settling, passive
  throughput measurement, weak-core tolerance, collapse/fault/timeout rejection,
  and fan release/power-off cleanup.
- Existing pool handoff, producer pause/resume, boot recovery-task creation and
  fresh-temperature telemetry tests.

One application-only flash is followed by a passive 180-second pool hold.
`monitor_pool.py` has no restart/resume commands and leaves a successful run
mining; on a fault it requests pause. No automated cold/warm series is performed.
Automatic fan control is restored to its pre-experiment setting before flashing.

## Hardware result

Successful single boot; first fresh 100 MHz ASIC temperatures were
27.29/26.64/26.64/27.13 C. The usual warmup and one warm supply cycle completed.
Passive 600 MHz qualification passed at **2960.91 GH/s**, with ASIC throughputs
98.44/98.75/98.98/99.25% of ideal. All 16 internal domains contributed; A1 D2
retained its known single zero-PASS core, which remains within qualification
limits. Pool handoff completed with all four ASICs at 600 MHz.

The following **181.1-second pool hold** completed successfully:

- Mean counter-derived hashrate: **2955.52 GH/s** over 44 windows.
- Mean per-ASIC hashrates: 733.73 / 739.81 / 739.34 / 742.64 GH/s.
- Mean API hashrate: 2954.13 GH/s; minimum 2935.96 GH/s.
- **19 accepted, 0 rejected shares during the hold**.
- Maximum ASIC temperature during the pool hold: 55.17 C.
- One ESP boot total; no automated restart, resume or cooling test.

The observer then closed, and a separate API read confirmed that the board
**remained mining at 600 MHz**, ASIC rail enabled, automatic fan control on,
and saved 1160 mV per half retained. The minimum fan setting remains 99%;
no permanent warmup fan change was saved to NVS. See `run-01/final-state.json`.

Installed application SHA256:
`e7578ef533d1498bf37a98e951fc540d8433baa377e0f3ee9db94388f41a9bf5`.
ELF SHA256:
`f9cd78d96e18c2e7211ec23a0f3f5ea4590b362cf2275995ffb9aa5829198b68`.
Boot log matched ELF prefix `f9cd78d96`. Application-only flash preserved
NVS and the matching web image. Binary, source snapshots and raw captures
remain local; curated result/identity summaries are tracked.

This verifies this startup and a short mining hold. It does not establish
cold/warm boot repeatability or long-term stability; those are the user's next
manual tests.
