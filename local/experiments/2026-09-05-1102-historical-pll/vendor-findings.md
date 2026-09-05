# Vendor reference checks

Read local `/Users/skot/Bitcoin/block-proto/MC3 Eratta.md`, `MC3 Abridged Register Map & Startup Pseudocode.pdf`, and `Block MC3 Datasheet clean.pdf`. PDF text was extracted locally with anydoc into this experiment directory; no document was uploaded or sent to another person/service.

The datasheet Table 12.1 matches the 37-entry table extracted from the exact old firmware. In particular all seven 450–600 MHz rows restored in this experiment match the vendor table. Section 12 recommends the supplied configurations and says alternative configurations at an identical output frequency should maximize FBDIV, with POSTDIV1 >= POSTDIV2. The changed 525 and 550 MHz rows used lower FBDIV than the historical values. This strengthens the case for testing historical values, but does not prove which clock characteristic changes or whether they caused domain failures.

The abridged register map labels temperature SDIF_STATUS (0x51024) bit 0 BUSY and bit 1 LOCK. Thus the preceding A3 STATUS=3 has both bits set. Datasheet section 11.1 step 7 expects STATUS=2 after the SDIF command enters conversion; step 8 then waits for DONE=1. Persistent STATUS=3 and DONE=0 over three attempts therefore shows that A3 did not reach the expected completed state; it is not itself proof of a hashing-domain crash. The meaning of LOCK beyond its field label is not expanded here.

Section 11.1 confirms reading DOUT clears DONE, supporting the new per-chip fresh-conversion check. Sensor clock setup 0x01010000 is documented as system clock 12.5 MHz / 2 = 6.25 MHz. We should not assume that the sensor directly takes its clock from a hashing PLL post-divider.

The clean `cd82a9e` reference configured sensors after completing the PLL ramp, but the actual dirty warm-boot image also contains later voltage-ramp changes. `9df1d31`, which resembles those changes, configures temperature at 100 MHz before ramping. Exact temperature initialization placement in the saved dirty image has not been established from source. Do not present clean-reference sensor order as a verified fact about that installed image.
