# Rigol confirmation of PLL-transition half-stack excursions

The instrument at `192.168.1.250` identifies as a Rigol DS2302A, firmware
00.03.06. Control worked over VXI-11. CH1 measures VDD−GND, CH2 measures
GND0−GND, and CH1−CH2 is the upper half-stack.

One guarded 400→425 MHz addressed launch was captured. A1 and A3 reached
425 MHz; the firmware disabled ASIC power during A2's update after sampling
1383/936 mV. A4, the requested 1000 ms delay, and work reapplication were never
reached. This is not a failed 1000 ms settling test.

| Excursion | Update inferred from serial order | Plateau median | Peak | Time above 1.30 V |
| --- | --- | --- | --- | --- |
| Lower high, upper low | A1 | 1.408 V | 1.476 V | 4.42 ms |
| Upper high, lower low | A3 | 1.396 V | 1.468 V | 5.22 ms |
| Lower high, upper low | A2 | 1.404 V | 1.472 V | 4.04 ms, ended by power shutdown |

Total VDD stays near 2.35 V during the half-stack shifts, with a 2.440 V
measured edge peak. The opposite half-stack falls near 0.94–0.96 V. Waiting
after the last PLL update cannot prevent these earlier voltage excursions.

The scope is left stopped, zoomed to 5 ms/div around the event. Normal 400 MHz
firmware is restored: 99.16% startup qualification, approximately 1.98 TH/s,
and about 1153–1155 / 1164–1166 mV in subsequent PCB telemetry. The diagnostic
is compiled out, and no persistent mining settings were changed.

## Data and measurement limits

- Both channels: DC coupling, 1× probe setting, 20 MHz bandwidth limit.
- Acquisition: 20 MSa/s, 28 Mpoints, 1.4 seconds, with 200 ms pretrigger.
- Saved raw NPZ: first 7 Mpoints per channel, from −200 to +150 ms; this covers
  every PLL-update excursion and power shutdown. The full 1.4 s acquisition
  remains in stopped scope memory. The requested smaller memory depth was not
  applied by the scope, so the verified 28 Mpoint setting is recorded.
- BYTE conversion: `(sample − yreference − yorigin) × yincrement`, with each
  channel's own recorded preamble. Time grids are checked equal before CH1−CH2.
- Channel voltage increments: CH1 8 mV, CH2 4 mV. Values above are approximate
  instrument readings, not millivolt calibration claims.
- Baseline scope readings: total 2.352 V, lower 1.184 V, upper approximately
  1.164 V. Total/lower read about 30 mV above firmware telemetry; no offset
  correction was applied.
- Pulse durations use 20 µs block averages and a 1.30 V threshold. ASIC labels
  come from serial update order, not a direct PLL clock measurement.
- Saturated portions after power-off are omitted; CH1−CH2 is valid only where
  both input channels are unsaturated. Plot shading preserves min/max ranges.

SCPI commands and the conversion formula follow the official
[DS2000A programming guide](https://beyondmeasure.rigoltech.com/acton/attachment/1579/f-0508/1/-/-/-/-/MSO2000A%26DS2000A_ProgrammingGuide.pdf).
Math control uses `CALCulate:SUB`, not `MATH`. The raw port 5555 stalled after
an unsupported query; a VXI-11 interface clear restored command service.
TCP_NODELAY improves transfer speed for this instrument's small RPC reads.

## Files and reproduction

`transition-detail.png` and `.pdf` show annotated excursions. The NPZ and
preambles JSON preserve the sampled data; summary and pulse JSON files hold
measurements. Evidence text files retain relevant serial output. Full logs,
binaries, BMPs and duplicate per-channel NPY files remain local and ignored.

Dependencies: numpy, matplotlib, python-vxi11, pyserial, Pillow. The temporary
bench environment is `/tmp/bitaxe-bench-env`.

```sh
/tmp/bitaxe-bench-env/bin/python analyze_capture.py
```

`capture_transition.py` changes scope acquisition settings and monitors a
separately flashed bench firmware. `download_stopped.py` only downloads the
frozen acquisition and does not rearm it. Neither script flashes the miner.

## Firmware hashes

- `scope-test.bin`: `17b7ad8dee4c3c9468494d5e57273289ee49b2e5e2dc7f09174f5f69f36a6244`
- `normal-400.bin`: `3bd51973bfdbb16342916ee65c67bc102e35e76174230cc0506616697b524eeb`
