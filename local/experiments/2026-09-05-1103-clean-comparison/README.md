# Unchanged-firmware cold resume versus reboot

Historical record: the recent automated restart/cooldown runners and firmware
probes were retired in the 2026-09-06 production cleanup. Their implementations
remain available in Git history. See
`../2026-09-06-1103-production-startup/README.md` for current firmware behavior.

2026-09-05. User requested the clean comparison after the previous series mixed
normal boot with recovery from a boot interrupted by pause.

Stock 1103, MAC 28:84:85:BA:96:08, IP 192.168.30.9,
USB `/dev/cu.usbmodem11434201`. No scope probes or scope operations.

## Fixed conditions

- No flashing or firmware changes during this series. Source commit `3c59e6a`.
- Installed application SHA256:
  `826a43260f8bf1cfac837c1b07650e76124cdd5279d47c93fd217f6e6041edcd`.
  ELF SHA256 `c384d5ce49d05e4f04a1d50258ee0ad323e49de3460dcabda058e6f312f6aeac`;
  each reboot log must match its reported prefix.
- Requested 600 MHz / 1160 mV per half; stock 1103 TPS configuration retained.
- Existing pool settings retained. Normal automatic fan / minimum 99% retained.
- Before each comparison action, pause ASIC power and verify <=100 mV with
  fan output exactly 100% for 180 seconds. Restore the original automatic fan
  setting immediately before resume or reboot.
- Record first fresh ASIC temperatures after initialization; cached OFF-state
  temperatures cannot establish the cold condition.
- Each successful run must qualify at 600 MHz, produce a newly accepted pool
  share and >2.8 TH/s, then hold for five minutes. Original startup qualification
  and host stop conditions are unchanged.

## Sequence

1. Normal software reboot, allowing startup to finish without pause, followed
   by an accepted-share hold. This establishes an actual mining session.
   If it fails, allow one explicitly recorded cooled reboot retry; if neither
   establishes mining, do not claim to have tested established-mining resume.
2. Pause that working miner, cool at 100%, then resume without rebooting ESP32.
3. Cool at the same settings and software reboot. Perform this control even if
   the resume fails, provided the failed run is confirmed powered off. If the
   resume fails, explicitly retain that different preceding outcome in analysis.

Record outcomes independently. No failure is converted into a pass by retrying
without a new run identity. Leave mining running if the final control passes;
otherwise leave ASIC power off. No input-power removal is claimed.

`compare.py` runs this finite sequence. `monitor_pool.py` captures serial logs
and whitelisted API fields, and shuts down on a failed qualification, persistent
temperature failure, hardware fault, unexpected reboot, temperature >=70°C,
or sustained hashrate below 2.8 TH/s. `analyze_pool.py` summarizes counter windows
and plots results. Logs, raw API captures and counter sample tables stay local;
curated summaries and plots are versioned. During resume, API rail telemetry
may remain cached while the power task runs initialization; fresh firmware
startup-guard samples, rather than that cached value, supervise the rail.

## Results

The two allowed baseline boots both failed final ASIC qualification. The
established-mining resume test and its subsequent reboot control were therefore
not run: neither baseline established the prerequisite working mining session.

| Normal boot | First fresh ASIC temperatures | Temperatures before first 600 MHz work | Final PLL to first work | Qualification | Missing domain |
|---|---|---|---:|---:|---|
| Initial | 25.48–25.98°C | 56.16 / 54.68 / 55.50 / 55.34°C | 1.599 s | 2781.86 GH/s | A3 D2, 39 zero-PASS cores |
| Cooled retry | 27.79–28.45°C | 56.00 / 54.68 / 55.67 / 55.67°C | 1.601 s | 2776.05 GH/s | A2 D0, 39 zero-PASS cores |

Both boot logs reported ELF prefix `c384d5ce4`, matching the same installed
binary. Neither boot was interrupted by a pause. Both completed warm 300 MHz
qualification, full supply discharge, four-chip reinitialization and the
unloaded ramp. No pool-work writes were logged between reinitialization and
first work. Both had zero thermal retries or persistent thermal failures.

The failed ASIC delivered 74.50% of ideal on the first boot and 74.43% on the
retry. The other three ASICs passed their aggregate throughput criteria; the
known single weak A1 D2 core was present but was not the failed domain.

The retry cooldown recorded 36 samples over approximately 181 seconds, all
with 100% fan output and ASIC supply <=100 mV. Both runs switched ASIC power
off after qualification failed. Final state: paused, 34 mV ASIC supply, saved
600 MHz / 1160 mV, automatic fan restored with minimum 99%; paused output 30%.

**Conclusion:** the latest firmware can reproduce whole-domain loss during an
ordinary uninterrupted boot. A resume-specific problem is not required for
this failure. This does not establish the underlying cause or rule out other
resume bugs. The requested paired resume-versus-reboot comparison remains
uncompleted because the normal-boot baseline could not mine. No claims about
fast voltage transients are possible without a scope.

The earlier successful holds used other build revisions. A useful next control
is the saved known-passing build versus this build under matched normal-boot
conditions, before attributing the difference to cold temperature or resume.
