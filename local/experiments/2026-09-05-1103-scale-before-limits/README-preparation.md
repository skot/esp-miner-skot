# TPS scale-before-voltage-limits fix and stock1103 validation

The first stock-board flash and a PMBus-only enable comparison both measured a1.208V ceiling despite1.6–1.92V commands. Restoring the board's own firmware (scale0.125) restored4-ASIC enumeration and2.32V output. Rebooting the unchanged new scale0.25 image a second time restored correct1.606V output for1.600V command, also detecting all4 ASICs.

The driver wrote VOUT_COMMAND/MAX/trim/margins before changing VOUT_SCALE_LOOP. This ordering is a candidate explanation for the first-boot/second-boot difference and the ceiling of half the programmed2.4V maximum. Register readback alone showed the intended values even while measured output was wrong. The internal-DAC explanation is an inference, not a directly measured internal state.

Fix: verify OPERATION is OFF, write/verify the configured voltage scale, then execute the rest of converter configuration, including all voltage commands and limits. Scale setup failures stop initialization. The previous later scale write is removed. This does not store anything in TPS NVM. Fresh Vout reads now also check the exponent-register read instead of using an unchecked conversion-helper read.

Validation probe deliberately writes a starting scale0.125 while OFF, before the new setup applies0.25. This prevents a lucky retained0.25 register state from hiding the ordering issue. The normal source defaults this test-only seed flag off. The original GPIO10 enable path / ON_OFF_CONFIG0x1F is restored for this run; PROTO_1103_PMBUS_ONLY is0.

Stock550kHz/scale0.25/13 11 8C 11 86. Same300MHz warmup, full ASIC supply cycle,1160mV per half, no-work600MHz ramp,1.5s settling, fresh temperatures and120s domain-counter observation. No scope under the user's explicit instruction. The run ends with ASIC power off. Tests cover scale ordering and errors, fresh reads, board modes and no-scope observation. Saved probe SHA256 fe782f3d7e70980c91719aeffeabaf8322779cd61155a8667627a5bc00295313.

The final probe also retains fresh raw ADC read checks while suppressing derived half-stack calculations during the bounded full supply cycle. This avoids combining sequential total/midpoint samples while the rail is moving; normal derived monitoring resumes after supply settling and total verification. The preceding never-flashed candidate is preserved in before-transition-guard/.
