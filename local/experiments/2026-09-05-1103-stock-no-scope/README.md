# First stock 1103 no-scope attempt: regulator-enable mismatch

User requested a log-only run on attached stock 1103 MAC 28:84:85:ba:96:08. Application-only flash passed verification; boot readbacks matched 550kHz / scale0.25 / compensation13 11 8C 11 86 / ON_OFF_CONFIG0x1F. No oscilloscope was used.

This attempt never reached warmup. All chain enumeration attempts at 800, 850, 900, 950 and 960 mV per half returned no response. Afterward, the API reported TPS total1206mV and PCB halves560/646mV, despite the final1920mV total command. No temperature values or hashing measurements existed. Host paused the device; rail fell nearzero. The serial supervisor was interrupted after preserving the failed boot.

The board's own backed-up application was restored as a comparison (../2026-09-05-1103-stock-old-control/). That image detected all four ASICs and reached2323mV total. Its logs showed ON_OFF_CONFIG0x1B (ignore hardware CONTROL pin), scale0.125, 550kHz and identical stock compensation. This isolated a firmware/power-configuration difference, not a demonstrated ASIC-chain hardware failure.

Next comparison changes only the new image's regulator-enable mode to PMBus-only, retaining scale0.25 and the successful warm-cycle ASIC sequence; it also logs actual startup voltage and powers down automatically on initial chain failure. See ../2026-09-05-1103-stock-pmbus-enable/.

Backups and hashes, serial.log, initial-api-state.json, manifest.json, source snapshots and build/test/flash logs preserve this attempt. The initial prepared manifest records intent; this README records the failed result. No waveform exists and no transient conclusion is supported.
