# Warm 1103x ramp comparison at 1140 mV

This follows the 1160 mV warmup in `../2026-09-04-warm-ramp/`, which reached
healthy warm 300 MHz but stopped on raw baseline scope peaks before any
higher-frequency update. This run lowers the temporary voltage target to
1140 mV per half-stack, retaining the same 1.20 V stop criterion. Saved NVS
voltage remains 1160 mV and frequency remains 400 MHz.

The prior board-off interval included fan cooling, rebuilding and flashing.
This is not a matched-temperature cold/warm A/B comparison. The full protocol,
fan lease behavior, per-step qualification and measurement limits are described
in the first run's README. The only firmware change for this comparison is the
temporary target voltage, now expressed as `PROTO_WARM_VOLTAGE_MV`.

The source probe flag has been disabled again after flashing. A normal image
was built separately and retained without flashing it onto the running test.

`capture_warm.py` owns USB and scope control. `analyze_warm.py` produces
per-domain interval data, temperature/voltage/throughput/fan plots, and raw
scope statistics. It excludes frozen `wait_scope` counter windows. Threshold
crossings count raw recorded samples; 20 µs block means are diagnostic only
and do not replace the peak-voltage acceptance criterion.

## Results

The initial 300 MHz qualification passed at 98.90% (1481.05 GH/s), with
first temperatures 37.69/36.70/37.36/40.33°C. After approximately four minutes
of warming, qualification passed at 99.50% (1490.08 GH/s), with
1138/1142 mV rails and 11.14 A regulator current. The warm qualification
snapshot was 53.03/52.20/52.53/55.67°C. All 16 domains averaged 98.54–99.75%
ideal passing throughput during complete warmup intervals. A1/A2 D2 averaged
99.29/99.23%; no sampled domain had a zero-PASS core during warmup.

At fixed 300 MHz, A1 D0's FAIL fraction decreased from 2.18% in the first
30-second window to 0.56% in the last, and A2 D1 decreased from 2.09% to
1.06%. Counter epochs were rearmed explicitly twice during warmup; these
are not a controlled demonstration of temperature alone causing improvement.
D2 started healthy and stayed healthy. See `warmup-windows.json` for the
complete interval definitions, temperatures and all domains.

The **warm 300 MHz scope baseline rejected before any 325 MHz update**:
raw lower/upper peaks 1.208/1.196 V, with no clipping. Mean scope readings
were 1.15346/1.15714 V, approximately 15 mV above the board telemetry.
696 lower-half samples were >=1.20 V; all were isolated recorded samples.
At 2 MSa/s and 20 MHz bandwidth this does not establish analog pulse widths.
Maximum 20 µs block means were 1.1572/1.1610 V. The raw peaks were not
substituted with these averages, and no offset correction was applied.

The host sent X after 2.404 seconds of waveform downloading and ASIC power
was off at boot timestamp 328.578 s. The sampled guard's maxima were
1139/1144 mV. No `WARM TRIAL`, PLL update above 300 MHz, or recovery hold
occurred. Thus **the warm high-frequency transition hypothesis is untested**.

Both warmups establish a balanced thermal starting point. The next step is
to validate the baseline voltage measurement and available headroom before
repeating the warm high-frequency ramp. These steady-state isolated sample
peaks differ from the earlier millisecond-scale transition plateau, but
this experiment cannot establish that they are measurement artifacts.

## Final state and checks

ASIC power is off; the loaded image is this guarded 1140 mV warm-ramp probe.
It will rerun on an ESP reboot while that image remains installed. In the
working source the warm-ramp probe flag is disabled. The normal image was
built successfully and retained locally, but not flashed because ordinary
400 MHz startup previously exceeded the sampled voltage limit.

The temporary fan lease ended. Read-only API verification showed fan 70%,
2730 RPM, auto fan enabled, saved minimum 99%, manual 100%, target 60°C;
saved frequency/voltage remain 400 MHz / 1160 mV. `actualFrequency=300` is
retained software state after power-off, not evidence of active hashing.

Probe and disabled-probe builds passed. Counter continuity/rate assertions,
Python compilation and whitespace checks passed. Waveform and warmup plots
were inspected. Full logs and exact firmware/source snapshots remain local;
`evidence.txt`, `sha256.json`, per-domain CSV/JSON, raw waveform bytes and
scope preambles provide the reproducible evidence.
