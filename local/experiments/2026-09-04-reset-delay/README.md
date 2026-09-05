# 2026-09-04 work-reset delay investigation

No minimum delay was established. Testing stopped on the 1200 mV sampled
half-stack cutoff before any nonzero-delay 425 MHz measurement completed.
The normal 400 MHz firmware was rebuilt with the diagnostic disabled.

| Run | Completed 425 MHz measurements | Stop reason |
| --- | --- | --- |
| sweep-boot1 | 0 ms; first A1/A2 D2 57.63/56.17%, second 99.53/99.16% | Quiesced rollback sampled 1373/946 mV |
| sweep-boot2 | 0 ms; A1/A2 D2 0/0% | Live rollback sampled 943/1374 mV |
| sweep-powercycles-v4 | None | Concurrent ADC read failed at 400 MHz baseline |
| sweep-powercycles-v5 | None | A2 PLL update toward 425 MHz sampled 1373/946 mV; no delay or final work reapplication occurred |

The second pilot window included another SPDLOG arm. That arm writes
GLOBAL_SPD and WORK_CFG with spd_go and rst_fifo actions; it is not passive
observation. This is a confound, not proof that waiting alone recovers D2.

The final harness serializes ADC averaging/calibration, keeps the 1200 mV
cutoff, and plans whole-rail power-off between trials. No final-harness trial
reached software restart; progress retention and the full multi-boot sweep
remain unvalidated on hardware. Do not interpret aborted-trial output as a
failed 1000 ms settling result. Voltage pairs logged after power shutdown can
combine old total-voltage telemetry with a falling midpoint and are not valid
steady-state measurements.

CSV/JSON files contain completed pilot measurement windows. Evidence text
files retain experiment-specific log lines. Full serial/build/flash logs and
exact binary images are retained locally and excluded by this directory's
.gitignore. No persistent mining settings were changed.

To regenerate pilot summaries:

```sh
python3 analyze.py sweep-boot1.log
python3 analyze.py sweep-boot2.log
```

## Firmware SHA-256

- `normal-400-before.bin`: `d23ed1d5f1f6299ccee2464ed04b1d1b187e3aca1b46ca01d8fc03d4e4e33ec3`
- `sweep.bin`: `896d400d82bf049373d3101316256aad810eee2a9805e052d24c9553029843a4`
- `sweep-v2.bin`: `2c609e2a3aa4626205bead7ee6f68e92b465dfc3bec04d681bf327f6c739ebcd`
- `sweep-v4.bin`: `36bddb02986e4185e125b32ecfa060e99efe35237f4fcd319120febebb90aaa3`
- `sweep-v5.bin`: `dcbda0e0534a30f45ad30919b8be3506dac445e1f0d5d9dd8cfa01ce6a6f59eb`
- `normal-final.bin`: `3bd51973bfdbb16342916ee65c67bc102e35e76174230cc0506616697b524eeb`
