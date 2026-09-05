# Stock 1103 log-only hardware test

User explicitly connected a stock 1103 and requested flashing/testing without an oscilloscope. No scope commands are used, no waveform is captured, and no scope acknowledgement is fabricated. The explicit PROTO_WARM_NO_SCOPE option automatically starts the warm supply-cycle/ramp after warmup and removes only the scope handshake requirement. A focused test checks the full 120-second no-scope observation, manual stop and failure paths, and confirms ordinary scope mode still requires acknowledgement.

Connected USB /dev/cu.usbmodem11434201, MAC 28:84:85:ba:96:08. NVS confirms proto / 1103. An existing ESP-IDF monitor was terminated to release that port. Backed up the factory application, header/NVS and OTA selection; erased OTA entries confirm factory boot. Application-only flash at 0x10000 passed hash verification. Saved NVS settings remain unchanged; the bench temporarily controls voltage, frequency and fan.

Firmware probe SHA256 ce69bffe178c7d9f08e5351d5f1557d11671a88a633ead672a426d666b69b2a9. Boot readbacks confirm 550kHz, VOUT_SCALE_LOOP 0.25, compensation 13 11 8C 11 86, ON_OFF_CONFIG 0x1F. Sequence otherwise matches the prepared stock experiment. Source snapshots and normal.bin are retained; all working-source bench flags are restored off. The saved probe remains the explicit no-scope experiment and powers ASICs off after observation.

Voltage and throughput thresholds remain report-only. Temperature checks, fresh TPS and powered ADC validity, bounded transitions and manual stop remain active. Without probes this run can establish counter-derived throughput and temperature behavior, but cannot characterize fast voltage transients or independently calibrate board voltage offsets.
