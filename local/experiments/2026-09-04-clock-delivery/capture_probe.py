"""Record one existing firmware bench trial and a synchronized DS2000A capture."""
import json
import re
import socket
import time
from pathlib import Path

import numpy as np
import serial
import vxi11
from PIL import Image

OUT = Path(__file__).resolve().parent


def block(data):
    if not data.startswith(b"#"):
        raise ValueError(f"Missing SCPI block header: {data[:80]!r}")
    n = int(chr(data[1]))
    length = int(data[2:2 + n])
    payload = data[2 + n:2 + n + length]
    if len(payload) != length:
        raise ValueError("Truncated SCPI data")
    return payload


scope = vxi11.Instrument("192.168.1.250")
scope.timeout = 15
try:
    scope.open()
    scope.client.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    scope.clear()
    print(scope.ask("*IDN?"), flush=True)
    # 1.4 seconds; verified sample rate and memory recorded below. Keep both channels DC coupled and 20 MHz limited.
    for cmd in [":STOP", ":TIM:SCAL 0.1", ":TIM:OFFS 0.5",
                ":ACQ:MDEP 28000000", ":ACQ:TYPE NORM",
                ":CHAN1:SCAL 0.2", ":CHAN1:OFFS -1.92",
                ":CHAN2:SCAL 0.2", ":CHAN2:OFFS -0.96",
                ":CALC:MODE SUB", ":CALC:SUB:SA CHAN1", ":CALC:SUB:SB CHAN2",
                ":CALC:SUB:VSC 0.1", ":CALC:SUB:VOFF -0.96",
                ":TRIG:MODE EDGE", ":TRIG:EDGE:SOUR CHAN2",
                ":TRIG:EDGE:SLOP POS", ":TRIG:EDGE:LEV 1.22", ":RUN"]:
        scope.write(cmd)
    settings = {q: scope.ask(q) for q in [":TIM:SCAL?", ":TIM:OFFS?",
                ":ACQ:MDEP?", ":ACQ:SRAT?", ":TRIG:EDGE:LEV?",
                ":CHAN1:BWLimit?", ":CHAN2:BWLimit?", ":CHAN1:PROB?", ":CHAN2:PROB?"]}
    (OUT / "transition-settings.json").write_text(json.dumps(settings, indent=2) + "\n")
    print("Configured:", settings, flush=True)
    armed = False
    forced = False
    deadline = time.monotonic() + 100
    with serial.Serial("/dev/cu.usbmodem11434401", 115200, timeout=0.5) as port, \
            (OUT / "transition-serial.log").open("w", buffering=1) as log:
        while time.monotonic() < deadline:
            line = re.sub(r"\x1b\[[0-9;]*m", "", port.readline().decode(errors="replace"))
            log.write(line)
            if any(token in line for token in ["CLOCK_PROBE BEGIN", "CLOCK_PROBE TRIAL",
                    "CLOCK_PROBE TIMING", "CLOCK_PROBE RESULT", "CLOCK_PROBE END",
                    "SAFETY STOP", "ASIC initialized successfully"]):
                print(line, end="", flush=True)
            if "CLOCK_PROBE BEGIN" in line and not armed:
                scope.write(":SINGle")
                armed = True
                print("Scope armed:", scope.ask(":TRIG:STAT?"), flush=True)
            if "CLOCK_PROBE TRIAL" in line and armed and not forced:
                # Hardware edge can trigger first; otherwise force now. The
                # 200 ms pretrigger record covers host/serial scheduling delay.
                scope.write(":TFORce")
                forced = True
                print("Scope force command sent at trial marker", flush=True)
            if "CLOCK_PROBE END" in line:
                deadline = min(deadline, time.monotonic() + 5)
    state = scope.ask(":TRIG:STAT?")
    if not armed or not forced or state != "STOP":
        raise RuntimeError(f"No completed synchronized capture: armed={armed} forced={forced} state={state}")
    bmp = block(scope.ask_raw(b":DISP:DATA?"))
    (OUT / "transition-screen.bmp").write_bytes(bmp)
    Image.open(OUT / "transition-screen.bmp").save(OUT / "transition-screen.png")
    arrays = {}
    preambles = {}
    for channel in ["CHAN1", "CHAN2"]:
        scope.write(":WAV:SOUR " + channel)
        scope.write(":WAV:MODE RAW")
        scope.write(":WAV:FORM BYTE")
        scope.write(":WAV:POIN 28000000")
        preamble = scope.ask(":WAV:PRE?")
        preambles[channel] = preamble
        # Save the full 1.4 second acquisition for all four probe stages.
        points = int(preamble.split(",")[2])
        chunks = []
        for start in range(1, points + 1, 250000):
            stop = min(points, start + 249999)
            scope.write(f":WAV:STAR {start}")
            scope.write(f":WAV:STOP {stop}")
            payload = block(scope.ask_raw(b":WAV:DATA?"))
            if len(payload) != stop - start + 1:
                raise ValueError(f"Wrong point count for {channel} {start}:{stop}: {len(payload)}")
            chunks.append(payload)
            if stop % 2000000 == 0:
                print(channel, stop, "points", flush=True)
        arrays[channel] = np.frombuffer(b"".join(chunks), dtype=np.uint8)
        print(channel, preamble, "saved", len(arrays[channel]), "points", flush=True)
    np.savez_compressed(OUT / "transition-raw.npz", **arrays)
    (OUT / "transition-preambles.json").write_text(json.dumps(preambles, indent=2) + "\n")
    print("Capture saved; scope left stopped on the event", flush=True)
finally:
    scope.close()
