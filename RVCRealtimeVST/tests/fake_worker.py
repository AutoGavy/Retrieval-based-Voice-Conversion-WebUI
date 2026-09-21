"""CPU-only IPC fixture. Never packaged with the plugin."""
import array
import importlib.util
import json
import mmap
from pathlib import Path
import sys
import time

args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
cfg = json.loads(Path(args["--config"]).read_text())
spec = importlib.util.spec_from_file_location("real_protocol", Path(cfg["rvc_root"]) / "rvc_worker.py")
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)
shared = mmap.mmap(-1, w.MAP_BYTES, tagname=args["--map"], access=mmap.ACCESS_WRITE)
request = w.WinEvent(args["--request"])
response = w.WinEvent(args["--response"])
assert w.read_value(shared, 4, "I") == w.PROTOCOL_VERSION
w.write_status(shared, w.STATUS_READY, "CPU test fixture")
resets = 0
try:
    while True:
        if request.wait(1000) == w.WAIT_TIMEOUT:
            continue
        if w.read_value(shared, 8, "i") == w.STATUS_STOP:
            break
        frames = w.read_value(shared, 20, "I")
        seq = w.read_value(shared, 12, "I")
        resets += bool(w.read_value(shared, w.RESET_OFFSET, "I") & 1)
        data = array.array("f")
        data.frombytes(shared[w.INPUT_OFFSET:w.INPUT_OFFSET + frames * 4])
        stalled = data[0] < 0
        if stalled:
            time.sleep(0.4)
        data[-1] = float(resets)
        shared[w.OUTPUT_OFFSET:w.OUTPUT_OFFSET + frames * 4] = data.tobytes()
        w.write_value(shared, 56, "f", 400.0 if stalled else 1.0)
        w.write_value(shared, 16, "I", seq)
        w.write_value(shared, 8, "i", w.STATUS_READY)
        response.set()
finally:
    request.close()
    response.close()
    shared.close()
