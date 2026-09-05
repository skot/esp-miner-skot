# Deferred temperature reads: 600 MHz reached, A3/A4 conversion fails

2026-09-05. This current-firmware comparison retained 1160 mV per half, warmed at 300 MHz, held ASIC reset without cycling Vcore/VDDIO, reinitialized at 100 MHz, and completed the historical 25 MHz PLL steps without work or thermal conversions. The first conversion occurred after the 1500 ms settling interval at 600 MHz.

All four PLLs reported lock at 600 MHz (boot 207.363 s). A3 **and A4** then returned raw zero, DONE=0, STATUS=3 on all three attempts, with successful register reads. A1/A2 remained valid around 59.6/58.1°C. The bench stopped at 210.778 s and disabled power at 210.797 s, before sending any post-reset work.

Removing thermal conversions during the ramp did not restore correct sensor operation. This is not evidence that hashing domains crashed: no post-reset work or throughput measurement occurred. Vendor documentation identifies STATUS bits 0/1 as BUSY/LOCK; the failed devices remained BUSY without DONE.

Scope CH1 remained 50 ohms. Powered peaks were TPS 2.592 V, lower half 1.800 V and upper difference estimate 1.444 V. The 50 µs acquisition was unclipped before deliberate power-off; its complete-record audit rejected subsequent CH1 clipping and the missing first-work launch. It cannot resolve 20 µs averages or rule out faster peaks. These uncorrected peaks are not a demonstrated cause of the sensor failure.

Probe SHA256: `51daf07615fd5cb9d5866221d0e667fc104ad79cae19313b2862969dbc11b56c`. See serial.log, thermal-summary.json, phase-summary.json and deferred-start/. The next comparison cycles both ASIC supplies and restores the startup voltage ramp: ../2026-09-05-1102-full-power-cycle/.
