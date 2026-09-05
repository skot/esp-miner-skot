"""Download the frozen acquisition; never arm or run the scope."""
import json
import socket
import time
from pathlib import Path
import numpy as np
import vxi11

p = Path(__file__).resolve().parent
s = vxi11.Instrument("192.168.1.250")
s.timeout = 20
try:
    s.open()
    s.client.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    print("RPC read size", s.max_recv_size, flush=True)
    s.clear()
    assert s.ask(":TRIG:STAT?") == "STOP"
    memory = int(s.ask(":ACQ:MDEP?"))
    arrays, preambles = {}, {}
    for channel in ["CHAN1", "CHAN2"]:
        s.write(":WAV:SOUR " + channel)
        s.write(":WAV:MODE RAW")
        s.write(":WAV:FORM BYTE")
        s.write(f":WAV:POIN {memory}")
        preamble = s.ask(":WAV:PRE?")
        preambles[channel] = preamble
        points = int(preamble.split(",")[2])
        print(channel, preamble, flush=True)
        # The critical interval is the first 0.35 s, including 200 ms before
        # the trigger and every addressed PLL update. Keep explicit bounds.
        read_points = min(points, 7000000)
        arr = np.empty(read_points, dtype=np.uint8)
        t0 = time.monotonic()
        for start in range(1, read_points + 1, 250000):
            stop = min(read_points, start + 249999)
            s.write(f":WAV:STAR {start}")
            s.write(f":WAV:STOP {stop}")
            data = s.ask_raw(b":WAV:DATA?")
            assert data.startswith(b"#")
            n = int(chr(data[1])); length = int(data[2:2+n])
            payload = data[2+n:2+n+length]
            assert len(payload) == stop-start+1
            arr[start-1:stop] = np.frombuffer(payload, dtype=np.uint8)
            if start == 1 or stop % 1000000 == 0:
                print(channel, stop, "points in", round(time.monotonic()-t0, 2), "s", flush=True)
        arrays[channel] = arr
        np.save(p / f"transition-{channel}-raw.npy", arr)
        (p / "transition-preambles.json").write_text(json.dumps(preambles, indent=2)+"\n")
    np.savez_compressed(p / "transition-raw.npz", **arrays)
finally:
    s.close()
