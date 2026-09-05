# 1102 warm-ramp test with 1300 kHz TPS configuration

## Outcome

**1102 also produces the large transition transient with the matching new
firmware sequence.** Warm 300 MHz qualified at 99.49%. The first 300→325 MHz
broadcast step produced lower-half peaks of **1.644 V raw / 1.577 V sliding
20 µs mean**. The upper estimate peaked at **1.416 V raw / 1.3139 V mean**.
Both scope limits were exceeded; the supervisor sent X and firmware powered
off. No step above 325 MHz or ten-second 325 MHz qualification was attempted.

TPS output movement is very similar to 1103, while the midpoint excursion is
larger. D2 remained healthy during the approximately 2.9 seconds of complete
counter intervals after launch. This demonstrates that the event is not unique
to 1103 hardware. It does **not** establish what transients occurred with the
older 1102 firmware: this trial uses the new compound work/PLL test sequence,
which differs from the old startup ramp. It also does not establish that such
excursions are harmless or identify the command responsible for each edge.

## Identity, preservation, flash and boot

The user confirmed 1102 powered and connected, with CH1 moved to its TPS output
caps and CH2 to GND0−GND. Initial serial/API identity:

- USB `/dev/cu.usbmodem11434401`, MAC `10:B4:1D:D5:CA:14`.
- HTTP `192.168.30.193`, hostname `bitaxe-ca14`, boardVersion `1102`.
- Old image `v2.14.0-40-gcd82a9e-dirty`, saved 600 MHz / 1160 mV.
- Initial API: 46.76°C and 2046.02 GH/s at 600 MHz. The brief initial serial
  sample reported about 368.98/374.08/558.18/738.44 GH/s on A1–A4. This was
  already an underperforming state; no internal-domain counters were captured
  from the old firmware in this turn.

The actual partition table and both OTA select records were read. The factory
app is at 0x10000, size 4 MiB, and both OTA records were erased. The entire
old factory partition was backed up as ignored `previous-factory.bin` before
writing. Its ELF hash begins 3186ecc062dd0201, matching the user's July 24
boot header; full metadata and hashes are in `firmware-identities.json`.

Only the prepared app was written at 0x10000; esptool verified the data hash.
NVS, web assets, bootloader, partitions and OTA selection were not written.
Prepared image SHA256:
`87478c5be3423a8f393a23afc19aa055dd6a8d23b18efcebb00bee01c64f6bfe`.
The first post-flash reset produced no captured serial data and no API response.
The supervisor was interrupted cleanly, then a second esptool reset produced
normal boot. `capture-no-boot.log` and `reset.log` retain this history. The
cause of the first boot/reset failure was not established; the image was not
modified or reflashed to obtain the successful boot.

Boot readbacks confirmed 1300 kHz, scale loop 0.125, compensation
13 20 C6 19 C6, and ON_OFF_CONFIG 0x1F. GPIO10 was held low; TPS READ_VOUT
was 0.05 V before ASIC power-up. WARM BOARD reports 1102, no PCB ADC sense,
GPIO+PMBus shutdown. Installed version is `v2.14.0-45-gbe978c3-dirty`.

## Protocol and scope

The exact saved test image from `../2026-09-04-1102-firmware/` was used;
no firmware source change or rebuild occurred during this run. Temporary
1140 mV per half, initial 100–300 MHz qualification, reduced-fan warmup,
all ASICs >=52°C and hottest >=54.5°C for ten seconds, then warm baseline
qualification. The first recorded temperature snapshot was about 44–45°C;
this was not an ambient-cold startup.

CH1 TPS output caps: **50 Ω**, DC, 1×. CH2 GND0−GND: 1 MΩ, DC, 1×.
Both bandwidth limits are 20 MHz. Initial, before/after capture and final
readbacks confirm these settings; CH1 remains FIFT throughout. CH1−CH2 is
an upper-half estimate mixing measurement locations and may include
interconnect/ground differences.

The same scope protocol captures 700,000 points/channel at 2 MSa/s over
−75 to +275 ms. No clipping occurred. The launch took 192 ms and fits the
record with the required post-launch margin. Acceptance remains raw peaks
<1.25 V and every sliding 20 µs mean <1.22 V. This is an after-event waveform
audit, not instantaneous protection. The independent 20 ms firmware guard
checks total voltage <2.40 V because 1102 has no midpoint ADC. It cannot detect
half-stack redistribution; logs explicitly report PCB balance unavailable.

## Measurements

Temperature gate completed at boot time 243.672 s. Warm 300 MHz qualified at
1490.01/1497.60 GH/s (99.49%). Baseline scope capture passed: lower/upper-estimate
raw maxima 1.204/1.188 V; sliding-mean maxima 1.1556/1.1538 V.

Trial began at 257.656 s; launch completed at 257.848 s. First post-launch
A1/A2/A3/A4 temperatures were 55.01/54.02/54.84/54.84°C.

| Metric | 1103 polymer, 550 kHz | 1103 ceramic, 550 kHz | 1102, 1300 kHz |
|---|---:|---:|---:|
| TPS pre-event baseline | 2.29450 V | 2.29927 V | 2.29968 V |
| TPS raw min / max | 2.152 / 2.416 V | 2.160 / 2.416 V | 2.144 / 2.424 V |
| TPS sliding 20 µs min / max | 2.1792 / 2.3974 V | 2.1752 / 2.4090 V | 2.1802 / 2.4132 V |
| TPS mean overshoot | +102.90 mV | +109.73 mV | +113.52 mV |
| TPS mean undershoot | −115.30 mV | −124.07 mV | −119.48 mV |
| Lower raw peak | 1.420 V | 1.424 V | 1.644 V |
| Lower sliding 20 µs peak | 1.4038 V | 1.4071 V | 1.5770 V |
| Upper estimate raw peak | 1.3240 V | 1.3320 V | 1.4160 V |
| Upper estimate sliding 20 µs peak | 1.2684 V | 1.2804 V | 1.3139 V |

`board-comparison.png` overlays the baseline-subtracted 20 µs averages aligned
on each lower-half peak. This alignment is not an on-wire PLL-edge measurement.
At the 1102 lower peak, simultaneous TPS/lower/upper-estimate averages are
**2.4132 / 1.5770 / 0.8362 V**. Main output rises about 113.52 mV, while lower
rises 426.23 mV and upper falls 312.71 mV. Thus the total supply excursion alone
does not explain the much larger redistribution. At the later upper peak,
voltages are 2.2130 / 0.8991 / 1.3139 V.

Non-overlapping 20 µs block means put the lower half >=1.22 V for approximately
120 µs, including 60 µs >=1.30 V. These are coarse block-duration estimates;
acceptance uses sliding means, retained separately in `audit.json`.

The host sent X after 2.686 seconds of downloading. GPIO low was logged at
261.601 s, VCORE=0 at 261.608 s, and WARM END ASIC_power=OFF at 261.613 s.
The total-rail guard maximum was only 2275 mV and did not catch the fast scope
excursion. No further frequency command was issued.

Four post-launch snapshots provide three complete counter intervals, about
2.88 seconds. D2 throughput A1/A2/A3/A4 was **100.76/98.89/98.81/97.78%** of
ideal; all 16 domains had zero sampled zero-PASS cores. This is a short
observation of passing activity, not proof of uninterrupted clock delivery,
no sub-second disturbance, or sustained mining at the new frequency.

## Final state

ASIC power is off. Final API regulatorVoltage=46 mV, temp/temp2=0, hashRate=0,
fan 70% / 2729 RPM. API actualFrequency=325 is retained state. Saved values
remain 600 MHz / 1160 mV, auto fan, minimum 25%, target 60°C. The post-shutdown
API also reports the generic Power Fault Detected status; the captured stop
was initiated by host X, not a logged firmware guard trip. This run does not
diagnose the post-shutdown status flag separately.

The guarded 1102 test image remains installed and reruns after reset. Source
warm-probe mode remains disabled; no normal image was flashed. The old image
is backed up for later comparisons. Scope is STOP; **CH1 remains 50 Ω**, CH2
1 MΩ. See final API/scope JSON and per-capture input-settings JSON.
