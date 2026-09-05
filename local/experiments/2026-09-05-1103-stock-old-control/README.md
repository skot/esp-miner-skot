# Stock 1103 original-firmware initialization control

Restored the exact application backed up from the connected stock1103, MAC28:84:85:ba:96:08, after the new0x1F enable-path image failed enumeration. Application-only flash hash verification passed. Original image version is v2.14.0-43-g1926f37-dirty, not the older1102 positive-control image.

The host captured one software reboot. All four ASICs enumerated, beginning at100MHz and800mV per half. API voltage increased1604→1821→2083→2323mV during the startup voltage ramp. This establishes that the board still responds with its previous firmware. It is not a600MHz or sustained-pool-mining test: the control was paused after65seconds of observation while its diagnostic initialization continued.

Boot log: 550kHz, stock compensation13 11 8C 11 86, VOUT_SCALE_LOOP set0.125, final ON_OFF_CONFIG0x1B. The first init write used0x1F, then the configuration writer replaced it with0x1B. Old board configuration lacks GPIO ASIC-enable control. The new failed image used0x1F and scale0.25; those two differences were not isolated in this control.

Board was paused and ASIC rail verified54mV before the next flash. See serial.log, api-samples.jsonl, before-api.json, final-api.json and flash.log. No scope was used.
