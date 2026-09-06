# Stock 1103 production pool startup

Historical record: the recent automated restart/cooldown runners and firmware
probes were retired in the 2026-09-06 production cleanup. Their implementations
remain available in Git history. See
`../2026-09-06-1103-production-startup/README.md` for current firmware behavior.

2026-09-05, on `1103x-support` after fast-forwarding `1103x-balancing`
through `c91844e`. Stock board 1103, MAC 28:84:85:BA:96:08,
USB `/dev/cu.usbmodem11434201`, IP 192.168.30.9. No scope attached or used.

## Implementation

The supported 1102/1103 startup path now returns to normal pool mining after
qualification. All new bench selectors and the older staggered PLL diagnostic
are disabled. The sequence is:

1. Enumerate at 100 MHz / 800 mV per half, raise voltage, and qualify through
   300 MHz. Warm all ASICs: coldest >=52°C and hottest 54.5–60°C for 10 seconds.
2. Disable Vcore and VDDIO, verify discharge <=100 mV, then re-enumerate four
   ASICs at 100 MHz / 800 mV per half and raise voltage to the requested value.
3. Broadcast 25 MHz PLL steps without work or thermal conversions. Allow
   500 ms through 500 MHz, 1000 ms above it, and 1500 ms at the final target.
4. Obtain fresh temperatures, send qualification work, measure throughput,
   clear qualification software state, and release normal pool producers.

The stock 1103 TPS profile is 550 kHz / scale 0.25 / compensation
`13 11 8C 11 86`. Scale is programmed before voltage commands and limits,
while the regulator is OFF. The separate 1102 profile remains 1300 kHz.
No capacitor modifications were needed on this tested board.

The production path accepts requests from 100–600 MHz and 800–1160 mV per
half, checked before enabling initial power. Frequency rounds to 25 MHz;
targets <=300 MHz omit the warm supply cycle. Warmup is bounded to 10 minutes
and the unloaded PLL ramp to 20 seconds. Settings are retained on failure.

Each qualification requires >=97.5% ideal throughput and >=97% PASS rate
per ASIC. Each internal domain must contribute >=90% of an equal quarter
of its ASIC's PASS count. This tolerates an isolated weak core while rejecting
a substantially missing domain. Final qualification re-arms SPDLOG without
changing a PLL. No synthetic shares are submitted to the pool.

The startup guard checks fresh TPS and ADC read validity; during the intentional
supply cycle it checks raw ADC validity and defers derived half-stack voltages
until the supply settles. Sampled voltage thresholds remain report-only as in
the authorized experiments. Pause, hardware faults, invalid reads, startup
temperature >=70°C, or persistent invalid temperatures stop the sequence.
Normal TPS hardware and runtime thermal protection remain active.

Pool handoff clears qualification counters, pending results and old valid-job
flags without PLL writes. The producer retains the latest pool job during
ASIC downtime and resends it after reset, even without another notification.
Persistent runtime thermal-read failure after bounded retries now latches a
hardware fault, stops mining, and forces full fan speed.

Warmup temporarily leases fan control; saved settings are retained. The final
fan refinement keeps the paused-mode 30% floor but honors a higher manual fan
setting, making API-requested 100% cooling possible with ASIC power off.

Cold-resume testing exposed a second recovery issue: `app_main` returned after
an interrupted boot before creating mining workers or the protocol coordinator.
ASIC recovery could subsequently reach 600 MHz but had no pool tasks to feed
work. Proto now creates those waiting tasks once even when boot is paused or
fails. Their existing `ASIC_initalized` gates defer hardware work until recovery
succeeds. Other hardware and self-test failure behavior is retained.

## Test method and builds

The supervisor requires 600 MHz, >2.8 TH/s and a newly accepted pool share
before each hold. It records serial counter telemetry and whitelisted API
fields, aborting on hardware fault, persistent invalid temperature, ASIC
temperature >=70°C, unexpected reboot or >60 seconds below 2.8 TH/s.
Existing pool credentials and endpoints are retained. Persistent operating
settings changed from 450 to 600 MHz at the authorized 1160 mV per half.
Saved auto fan / minimum 99% are restored after the full-fan cooldown.

| Runs | Build | Application SHA256 |
|---|---|---|
| 01 | Initial production startup | `3f47c073c6479ba500dabb467d0e8c72d450e143728d48e98bdea1b3436bb250` |
| 02–03 | Preflight and pool handoff refinement | `7f37958ed8e9d631b1049090d49730dae68118bc02f54d2b7b32bcfe0ed01b9f` |
| 04 | Same startup/handoff, plus paused manual fan control | `3d99dbfd15ad1d532ed12e9fb13534b7ceb851613fcb70ec502c406d34959103` |
| 05 onward | Also keep pool workers available after interrupted Proto boot | `826a43260f8bf1cfac837c1b07650e76124cdd5279d47c93fd217f6e6041edcd` |

The first flash included matching web assets. Later flashes updated only the
application and verified the written hash. `preflight-build/` is an intermediate
build that was not flashed. Binary artifacts, exact hashes and source snapshots
are retained locally; raw serial/API logs and binaries are ignored by Git.

The user refined the final cooldown to require 100% fan speed. The original
30% cooldown controller was stopped before resume. An API PATCH on the old
build returned success and stored manual 100%, but actual output remained 30%
while paused. The fan change fixed that override. After flashing, the first
cooldown supervisor attempt stopped because early boot resumed rail setup after
an initial OFF reading. This was preparation, not a completed resume attempt.
The revised supervisor waits for boot to settle and sustained OFF readings.
Boot startup then honored the pending pause and switched ASIC power off.

The final cooldown holds the fan at a verified 100% for 180 seconds with
ASIC supply <=100 mV. It restores automatic fan mode for the ordinary warmup
and tests resume without rebooting the ESP32. Fresh initial temperatures are
recorded after ASIC power returns; cached temperatures while OFF are not used.

## Results

| Run | Start / condition | Pool hold | Mean ASIC counters | Accepted / rejected during hold | Result |
|---|---|---:|---:|---:|---|
| 01 | Cold boot, first fresh 27.62–28.28°C | 1801.7 s | 2949.43 GH/s | 60 / 0 | PASS |
| 02 | Handoff build flash, 36.20–37.19°C | 302.1 s | 2951.04 GH/s | 38 / 0 | PASS |
| 03 | Software reboot after mining, 38.18–39.17°C | 301.9 s | 2955.93 GH/s | 45 / 0 | PASS |
| 04 | Paused boot, 180 s at 100% fan, 26.96–27.79°C | None | Qualification 2963.04 GH/s | None | FAIL: ASICs ready, but pool tasks absent |
| 05 | Recovery-task fix, repeated 180 s at 100% fan, 27.46–28.12°C | None | Qualification 2773.80 GH/s | None | FAIL: A2 D0 all 39 cores zero PASS |
| 06 | Planned pause/resume from established mining | Not run | — | — | Stopped because run 05 failed |

The three successful holds total 40.09 minutes and 143 accepted shares during
those holds, with zero rejects. Maximum ASIC temperature during each hold was
55.17 / 54.18 / 57.48°C respectively. All recorded temperature streams were
fresh with zero bounded thermal retries. The 30-minute hold's steady per-ASIC
means were 730.13 / 738.72 / 737.66 / 742.90 GH/s.

Run 04 exposed the missing-task recovery bug described above. Run 05 reached
300 MHz with all domains contributing, then failed after the warm supply cycle
and unloaded ramp: A2 D0 had 39 zero-PASS cores; A2 throughput was 557.27 GH/s
(74.42% ideal). A1 was marginally below its throughput gate at 97.48%, while
A3/A4 were 99.14/99.39%. No pool-work writes were logged between reinitialization
and first work. Pre-first-work temperatures were 55.34 / 53.69 / 55.01 / 54.84°C.
The cause is unresolved; these data do not establish that temperature alone or
background tasks caused the failure. Qualification thresholds were not relaxed.

The supervisor stopped on the failed qualification and cancelled the subsequent
active-mining resume test. Final state: ASIC power OFF (34 mV sampled), mining
paused, saved 600 MHz / 1160 mV, automatic fan restored with minimum 99%.
Paused mode is at its normal 30%. The latest installed build is the recovery-task
build. Its interrupted-boot worker creation is verified in logs and host tests;
successful pool handoff after that recovery still needs hardware validation.

Cold-resume repeatability is **not established**. The successful long hold and
reboot results must not be presented as a fully reliable cold-start solution.


## Validation and limits

Firmware builds and whitespace checks pass. Focused tests compile the real C
startup helpers, telemetry reader, handoff function and job producer with mocked
hardware/queues. Coverage includes ordering, no-work settling, thermal/read/
discharge/chain/pause/timeout failures, weak-core acceptance versus domain
collapse, shutdown, fan release, preflight validation and job resend after
reset. The paused fan API behavior is checked on hardware.
`test_recovery_tasks.py` compiles the real `app_main` tail and verifies worker
creation after interrupted Proto boot, plus unchanged self-test and other-board
failure behavior.

These runs test a combined recipe on one stock 1103, not individual causal
contributions or fleet reliability. The final fan refinement has its own build
identity; the 30-minute hold is not attributed to that later binary. Runtime
per-ASIC counters do not continuously qualify all 16 internal domains. A known
isolated weak core remains. No scope data was collected, so fast transients
cannot be assessed. These are ASIC counter hashrates backed by accepted pool
shares, not a pool-side statistical hashrate estimate. Physical input power
was not removed for the software reboot or ASIC-off resume tests.
