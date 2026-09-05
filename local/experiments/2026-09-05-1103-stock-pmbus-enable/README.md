# PMBus-only enable comparison: same startup failure

Same stock1103 and no-scope warm-cycle probe, with an explicit PROTO_1103_PMBUS_ONLY option disabling GPIO ASIC-enable use and selecting ON_OFF_CONFIG0x1B. Scale remained0.25, switching550kHz, compensation13 11 8C 11 86. The original-firmware control preceded this flash and had programmed scale0.125.

This first boot again failed all ASIC enumeration attempts. New startup logs measured **1208mV total at every command:1600,1700,1800,1900,1920mV**. The firmware then disabled both ASIC supplies and logged WARM END startup chain failure with successful PMBus shutdown. No warmup or post-reset work occurred.

Thus ignoring the CONTROL pin was insufficient to repair the failure. An identical software reboot, without reflashing or changing settings, then measured1606mV for a1600mV command and detected all four ASICs immediately. That follow-up is ../2026-09-05-1103-stock-repeat-boot/.

Probe SHA256 f11bd5f5e2be259d46fba4b780512a729f08c0409bc4dd2393a9b46e51a7679f. Source snapshots and flash/serial logs retained. Working-source PMBus-only and bench flags were restored0 after building. The repeat boot still used this saved image.
