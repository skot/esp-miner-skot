# Review: c91844e successful bench versus current startup

Read-only review requested after the slower-ramp trial. No firmware flashed or
settings changed during this review. Current branch is 1103x-support, HEAD
381ed6e plus uncommitted dashboard diagnostics and 2-second ramp changes.

## Reproducible reference

The successful stock 1103 image associated with c91844e is the archived
`../2026-09-05-1103-scale-before-limits/probe.bin`, SHA256
`fe782f3d7e70980c91719aeffeabaf8322779cd61155a8667627a5bc00295313`.
Its binary, ASIC initialization source, and MC3 source still match the saved
manifest. It ran synthetic work at 2964.58 GH/s for 120 seconds, with a single
persistently weak A1 D2 core but no whole-domain collapse.

The commit's default source has the warm-probe selectors disabled. The tested
image enabled warm ramp, report-only qualification, deferred work, deferred
thermal reads, full supply cycle, and no-scope operation. Checking out c91844e
and building defaults would not reproduce the successful experiment. That
bench also deliberately seeded TPS scale 0.125 while OFF before configuring
0.25; this test-only flag is disabled in current source. The voltage configuration
order and stock regulator profile remain intact.

## Material behavioral changes

| Aspect | Successful archived bench | Current firmware |
|---|---|---|
| Warmup | Hottest >=54.5 C and coldest >=52 C for ten seconds | All ASICs >=40 C for ten seconds (381ed6e) |
| Temperature immediately before first work | 57.48 / 56.16 / 56.98 / 56.98 C | Recent fast ramp 46.26 / 44.45 / 45.60 / 45.44 C; slow ramp 47.25 / 45.11 / 46.59 / 46.76 C |
| Final work configuration | Normal mining config, including a PLL EN=1 write; submit job; then start active SPDLOG observation | Mining config omits PLL EN write; non-strobing counter target selection and timer arm before job; submit job |
| After first job | GLOBAL_SPD and WORK_CFG reapplied, including SPD_GO and RST_FIFO, then counter reset/timer | No work/SPD/PLL configuration strobe during the qualification observation |
| Failure observation | Report-only throughput, observe 120 seconds even if deficient | About ten seconds of passive qualification, then power off if rejected |
| Ramp dwell | 500 ms through 500 MHz, 1000 ms above, 1500 ms final | Latest trial: 2000 ms from 300 through 575 MHz, final 1500 ms |
| Fan/observation | Sensor/counter snapshots with thermal fan control over the 120-second hold | 70% fan refreshed during unloaded ramp and final ten-second check |

bc8ab6e introduced the passive first-work launch as a diagnostic comparison.
83a29ff adopted it for production startup. Thus cleanup changed first-work
behavior as well as removing test code. The original successful bench called
MC3_start_qualification_work followed by MC3_start_active_spdlog_observation;
current startup calls MC3_start_qualification_observation. The omitted post-job
SPD/FIFO strobe is a plausible contributor given earlier recovery after combined
work/configuration reapplication, but its necessity is not established.

The shorter observation changes when failure is declared and could miss a late
recovery. It does not itself explain why domains have zero PASS during the
first ten seconds. The archived successful run did not show whole-domain
collapse during its measured hold.

## Checks against misleading explanations

- PLL_CONFIGS table, MC3_send_hash_frequency, mc3_get_pll_config,
  mc3_rolltime_for_frequency and MC3_read_temperatures match the archived MC3
  source byte-for-byte at function/table level.
- vcore.c matches the archived source. TPS546.c differs only in disabling the
  deliberate scale-seeding test. Stock 550 kHz / scale 0.25 / compensation
  13 11 8C 11 86 and 1160 mV/half remain in use.
- Pool handoff occurs after final qualification; current domain failures occur
  before pool work begins. Early production pool holds after c91844e totaled
  40.09 minutes near 2.95 TH/s with 143 accepted shares and zero rejects.
- 3c59e6a records a whole-domain failure before the passive-launch change and
  before the 40 C warmup change. 83a29ff also produced a successful 2.961 TH/s
  qualification and 181-second pool hold before unchanged-image resets failed.
- At the older warmup threshold, subsequent failures measured 2.595 and
  2.779 TH/s. After lowering to 40 C, recent trials repeatedly measured about
  2.045 TH/s with five missing domains. This makes lower launch temperature
  a strong suspect for the recent worsening, without proving causality.
- The slower ramp increased actual 300–600 MHz time from 7.907 to 24.380 seconds
  with no material improvement (2044.51 versus 2044.74 GH/s). Final PLL-to-work
  delay stayed 1591/1592 ms. Ramp speed is not supported as the cause of this
  latest worsening by that trial.
- Dashboard diagnostics were added after the failures were already present.
  Startup caching uses existing reads and adds no configuration strobes.

## Assessment and next controlled comparison

There is no isolated causal regression yet. The clearest recent change is the
roughly 10–12 C reduction in first-work temperature. First-work strobe ordering
is the other major difference from the original demonstrated sequence. Earlier
failures mean neither can be claimed as the sole explanation.

Use the exact saved successful image as a positive control on this board,
including its original warmup and launch sequence. Confirm repeatability before
porting changes back one at a time. Preserve present source and artifacts;
rebuilding default c91844e is not an equivalent control. Only the read-only comparison was performed; hardware was left unchanged.
