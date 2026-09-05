# 1102 temperature retry test: A3 conversion remains incomplete at 550 MHz

2026-09-05. **Implemented, built, flashed, and tested the per-ASIC temperature completion checks and bounded retries. A3 failed all three conversion attempts at 550 MHz. The firmware powered down before sending the first post-reset job; 600 MHz throughput remains untested by this sequence.**

## Result

- App-only flash verified by esptool on 1102, MAC `10:B4:1D:D5:CA:14`, serial `/dev/cu.usbmodem11434401`. Saved `probe.bin` SHA256: `5cb338a380780ac2ecede28dbac63fc28a653246b8d65a1408a8b220c253eb51`.
- TPS546 remains at 1300 kHz with compensation `13 20 C6 19 C6`; voltage target **1160 mV per half**. Normal work tasks remain gated throughout the bench.
- Warm 300 MHz qualification: **1489.64 GH/s, 99.47%** of ideal. Just before reset, A1/A2/A3/A4 were **55.17/54.18/55.01/55.01°C**. Zero interval rates in `wait_defer` reflect the expired baseline observation window, not newly failed domains.
- Warm reset began at boot **314.114 s**, leaving the ASIC rail on. Four chips reinitialized at 100 MHz by **314.761 s**. Every 25 MHz step through 550 MHz locked on all four ASICs.
- At 525 MHz, A3 returned raw `0x83C`, DONE=1, STATUS=0, **58.80°C**.
- 550 MHz lock: **326.541 s**. After the 1000 ms settling interval, the following attempts all failed on A3:

| Attempt | A3 result logged at boot time | DONE | STATUS | Raw DOUT | Converted value |
|---|---:|---:|---:|---:|---:|
| 1 | 328.088 s | 0 | 0x3 | 0 | −288.90°C |
| 2 | 328.748 s | 0 | 0x3 | 0 | −288.90°C |
| 3 | 329.408 s | 0 | 0x3 | 0 | −288.90°C |

Each attempt drained DOUT, verified DONE=0 on every ASIC, broadcast a new conversion, and waited for each ASIC's completion using a 500 ms shared conversion deadline. A3's raw/status reads succeeded; it did not assert DONE. Fresh attempts were separated by 100 ms. The three-attempt call took about 1.9 seconds. A1, A2 and A4 completed all attempts with STATUS=0 and valid temperatures; final values were **59.29/57.81/60.12°C** respectively.

The reader returned -1 for the unresolved A3 sample. The caller stopped at 329.432 s, GPIO10 went low at **329.439 s**, and shutdown completed by **329.451 s**. No 575/600 MHz step, first-work launch, post-reset domain counters or final two-minute observation occurred.

There were **224 logged samples per ASIC** across the full test. The only invalid samples were these three A3 results. No other retry or stale-DONE event occurred.

## What this establishes

This is a persistent failure to complete a thermal conversion over the retry interval, not a single bad temperature sample. Checking only A1 DONE in the old reader could allow reading A3 before it completed; the revised reader exposes that A3 remains incomplete across fresh attempts. STATUS 0x3 differs from the healthy 0x0; its individual bit meanings have not been verified here. The serial response can be read even while A3's thermal conversion does not complete.

No hashing-domain counters were collected after reset, so this does **not** establish a hashing-domain crash, sensor-only failure, or recovery capability. The result occurred before any new work was sent after reset. It does not prove that the ASICs were electrically idle or that reset cleared every internal work state.

Further comparison should distinguish the thermal sensor/conversion-clock state from a broader ASIC state problem, for example by logging documented PVT status/configuration and testing sensor-only reinitialization at a fixed PLL. Merely accepting an unlimited stream of invalid temperatures would hide the observed condition. No such additional intervention was performed in this run.

## Scope result

The corrected nine-second pretrigger fill succeeded: both channels contain **700,000 points at 50 µs spacing**, from −5.5 to +29.5 seconds. CH1 stayed **50 Ω** at the TPS output caps; CH2 stayed 1 MΩ at GND0. DC coupling, 1×, 20 MHz bandwidth were verified.

**There is no clipping during the powered test.** CH1 falls below its selected display range after deliberate shutdown, producing 274,984 clipped samples in the remainder of the long record. The supervisor therefore records `accepted=false`, also because no first-work launch occurred. This is a usable pre-shutdown waveform, unlike the previous run's empty acquisition. The host sent X during the completed-waveform audit after the firmware had already stopped; X did not cause this shutdown.

Raw, uncorrected measured peaks during the powered interval were TPS **2.552 V**, lower half **1.740 V**, and CH1−CH2 upper-half estimate **1.404 V**. Around the transition aligned with 550 MHz, the sampled TPS range was approximately **2.200–2.536 V**, with lower-half peak **1.720 V**. After the transition settled, during repeated failed temperature conversions, TPS was **2.328–2.360 V** (median **2.344 V**); lower/upper medians were approximately **1.140/1.204 V**.

These readings are not offset-calibrated. CH1 and CH2 use different physical locations; their difference is an upper-half estimate. The 50 µs sampling cannot resolve 20 µs averages and may miss faster extrema. Serial events were aligned approximately using the GPIO shutdown log and the observed rail fall, yielding a +0.4258 s scope-time shift; no hardware-synchronized serial trigger was recorded. The transient near the PLL update and later sensor failure do not by themselves establish causation. There is no sustained TPS collapse during the retry interval.

## Implementation, validation, and final state

See `README-preparation.md` for reader behavior and controlled variables. The new reader holds a transaction mutex, verifies each ASIC's fresh DONE state, logs raw/status data, and retries invalid results up to three times with a shared two-second budget. Individual serial transactions may slightly extend that budget. Valid >=70°C readings are never retried away; persistent invalid readings still stop the bench. No cached temperature fallback is used.

Focused host tests passed for transient/persistent zero, delayed A3 completion, stale DONE, failed reads/trigger, and hot-with-invalid cases. Existing work-ordering, abort, board-policy, and scope-audit tests passed. Probe and normal builds succeeded. Only the saved probe was flashed; all bench flags are restored off in working source and `build/esp-miner.bin` is the normal build.

Final API: **ASIC power off**, reported `coreVoltageActual=22` mV, fan **70% / 2730 RPM**. Scope STOP, CH1 50 Ω. Saved settings remain 600 MHz / 1160 mV. **The retry-enabled deferred-work probe remains installed and will rerun on reset.**

Artifacts include `serial.log`, `flash.log`, `thermal-samples.json`, `thermal-summary.json`, source/ELF/bin snapshots, test/build logs, `deferred-start/raw.npz`, scope audit and event analysis, and `deferred-start/annotated-waveform.png/pdf`. `thermal-change.patch` isolates this turn's C change relative to the preceding saved experiment.
