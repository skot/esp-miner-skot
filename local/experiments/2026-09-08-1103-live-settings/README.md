# AxeOS operating-point settings: live voltage, automatic frequency qualification

Stock 1103, 192.168.30.9, MAC 28:84:85:BA:96:08. Date: 2026-09-08.
No scope attached. Base checkpoint: cef666d55805674210828250f49df88420f05519.

## Delivered behavior

Voltage-only settings saves now change the regulator in at most 10 mV increments
while the ASICs continue mining. No PLL/work writes, ASIC reset, or ESP restart
are made for a voltage-only change. Settings remain persistent in NVS.

Fully live frequency changes were NOT made reliable in this investigation.
Frequency saves retain automatic ASIC power cycling, initialization, warmup,
unloaded PLL ramp and qualification. They do not require a manual ESP32 reboot,
but mining pauses during the ASIC sequence. The final UI says this explicitly.
The unsafe live-PLL implementations were removed from production source; the
MC3 driver is restored to the checkpoint implementation.

For stock 1103 targets above 300 through 600 MHz, boot/resume/qualification uses
the successful 1160 mV warmup and 1200 mV first-work/PVT-recovery sequence even
when a lower mining voltage is saved. After 30 seconds following successful
ASIC initialization, normal power management eases to the saved mining voltage.
The existing 40 C warmup, thermal/fault handling, and startup qualification are
preserved. Other boards and self-test do not receive the new startup-voltage
selection. The PVT recovery helper now logs the actual final frequency.

The power task tracks the last successful voltage command separately from the
measured voltage. Live writes are gated on initialized, unpaused, fault-free
ASICs, and repeated settings changes are coalesced for one second. Failed
voltage writes stop mining rather than pretending the command succeeded.

AxeOS no longer enables Restart after a live-only save. Pending display-related
restarts survive later saves and failures. The Proto preset frequency list is
limited to the existing supported 100..600 MHz range, and voltage presets now
include 1160/1170/1180/1190 mV. REST validation rejects unsupported Proto
frequency/voltage pairs before saving any field. The SystemASIC OpenAPI enum
now includes the MC3 value already returned by firmware; client regeneration
and mock type checking are included.

## Failed live-frequency experiments

1. `first-attempt`: gradual PLL table steps with mining active. All four PLLs
   locked on the first 600 -> 595.833 MHz step, but A3/A4 thermal sensors returned
   DONE=0, STATUS=3 and invalid data through bounded retries. The temperature
   guard correctly stopped mining. Voltage-only 1200 -> 1180 -> 1200 passed.
2. `pvt-only-attempt`: added PVT-only sensor reinitialization after each PLL
   step. Sensors recovered, but A1/A2 internal domains D1 and D2 lost PASS
   throughput. 156 cores had zero PASS; the remaining 468 produced roughly
   2.14 TH/s at 575 MHz. Returning to 600 MHz did not recover those domains.
3. `work-refresh-attempt`: attempted the existing WORK_CFG reset/apply before
   each step, waited for PLL settling, refreshed PVT, and reapplied the current
   pool job with new rolltimes and SPDLOG arming. This also left A1/A2 D1/D2 at
   zero PASS. It was removed. Neither PVT refresh nor this work-refresh variant
   was sufficient to make loaded PLL transitions reliable.

The local MC3 errata recommends reapplying work after PLL changes, motivating
attempt 3. Existing domain-balancing notes already record an unsuccessful
alternate-PLL/ping-pong experiment; that approach was not blindly repeated.
These failures do not establish the electrical root cause. No scope transient
measurements were available in this session.

Failed attempt logs, binaries (ignored locally), manifests, source snapshots,
and host-test scripts are archived in the named directories. Historical test
scripts refer to the source layout at the time they ran; the supported final
host test is `test_live_voltage.py` in this directory.

## Verification

- Firmware build and final live-voltage/startup host tests pass.
- AxeOS: 54 tests pass, including no restart flag for live-only saves, pending
  display restart preservation, and the Proto behavior note.
- Browser check: a live-only save showed “Saved settings,” marked the form
  pristine, and left Restart disabled. The final behavior note is visible.
- `run_stable.py` records the final on-device comparison. It checks that voltage
  changes leave mining active, that frequency requalification does not reset
  ESP uptime, that all previously active cores return, and that lower saved
  voltage is applied only after the boosted startup.
- `manifest.json` identifies the installed app and initial web image;
  `web-manifest.json` identifies the final web-only update. The UI rebuild did
  not change the installed application binary.

Final measured results are in `results.json`, `final.json`, and the phase epoch
files. The tests use complete fresh distinct core-counter windows, not just
pool-share estimates. Power is TPS546 output power, not wall power. This is
short functional testing, not a long-term stability or efficiency claim.

## Completed hardware comparison

| Phase | MHz | Saved mV/half | Mean TH/s | TPS output W | Fresh epochs |
|---|---:|---:|---:|---:|---:|
| boot-1180-hold | 600 | 1180 | 2.9663 | 47.89 | 8 |
| voltage-up | 600 | 1200 | 2.9670 | 49.84 | 11 |
| frequency-575-hold | 575 | 1200 | 2.8465 | 48.27 | 8 |
| frequency-600-voltage-1180-hold | 600 | 1180 | 2.9645 | 48.19 | 6 |
| final-restore | 600 | 1200 | 2.9653 | 49.93 | 11 |

All five holds passed. Each hold lasted 45–64 seconds after voltage settling.
The same previously inactive A1 logical core 105 was the only zero-PASS core;
the other 623 cores remained active. All four PVT temperatures were valid.
Both automatic frequency qualifications preserved ESP uptime. This was an OTA
boot and software-controlled settings test, not a physical cold-power-cycle test.

Final saved and running settings: **600 MHz / 1200 mV per half**. Final ESP
uptime was 842 seconds, startup qualification passed, and temperatures were
51.7–52.9 C. The saved 1180 mV test confirmed startup at 1200 mV followed by
the delayed decrease to 1180 mV.

Installed app SHA-256: `52186d223f037e311504760c6308c41d96c9f28e9b7c01278c2870a79202f739`.
Final web SHA-256: `11bcafab0589fe1e53f66b39f51523f789ef6d8e3226141fb867517f2682f9f7`.
