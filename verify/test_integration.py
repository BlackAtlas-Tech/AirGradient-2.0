#!/usr/bin/env python3
"""
test_integration.py -- cross-language Path B integration test.

Spawns the compiled C++ device_sim (which links the REAL firmware protocol code:
airgradientClient base + AgWireLink + AirgradientSerialClient) and drives it with
the REAL reference Sensor Hub agent (ag_wired_hub.HubProtocol). Exercises the full
handshake -> config-fetch -> post/ACK path, including a NAK-forced retransmit.

Run:  python3 test_integration.py            (from the verify/ directory)
Requires: a built ./device_sim  (see build step printed on failure).
"""
import os
import select
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# host/ modules live one level up in the packaged tree; support both layouts.
for cand in (os.path.join(HERE, "..", "host"), HERE):
    if os.path.exists(os.path.join(cand, "ag_wire.py")):
        sys.path.insert(0, cand)
        break

import ag_wire as W
from ag_wired_hub import HubProtocol

DEVICE_SIM = os.path.join(HERE, "device_sim")

passed = failed = 0
def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
        print(f"[PASS] {msg}")
    else:
        failed += 1
        print(f"[FAIL] {msg}")


def run():
    if not os.path.exists(DEVICE_SIM):
        print("device_sim not built. Build it with:")
        print("  g++ -std=c++17 -O2 -I../examples/OneOpenAir -I../src "
              "-I../src/Libraries/airgradient-client/src -Ihostshim \\")
        print("      device_sim.cpp ../examples/OneOpenAir/AgWireLink.cpp \\")
        print("      ../examples/OneOpenAir/AirgradientSerialClient.cpp \\")
        print("      ../src/Libraries/airgradient-client/src/airgradientClient.cpp -o device_sim")
        return 2

    proto = HubProtocol(nak_first=1)  # NAK the first MEASURES -> force one retransmit
    reader = W.FrameReader()

    proc = subprocess.Popen(
        [DEVICE_SIM],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    out_fd = proc.stdout.fileno()
    os.set_blocking(out_fd, False)

    import time
    deadline = time.time() + 25.0
    while True:
        if time.time() > deadline:
            proc.kill()
            check(False, "device_sim completed within timeout")
            break
        r, _, _ = select.select([out_fd], [], [], 0.05)
        if r:
            chunk = proc.stdout.read(4096)
            if chunk:
                for frame in reader.feed(chunk):
                    for resp in proto.handle(frame):
                        try:
                            proc.stdin.write(resp)
                            proc.stdin.flush()
                        except BrokenPipeError:
                            pass
        if proc.poll() is not None:
            # drain any final bytes
            try:
                tail = proc.stdout.read()
                if tail:
                    for frame in reader.feed(tail):
                        for resp in proto.handle(frame):
                            pass
            except Exception:
                pass
            break

    rc = proc.wait()
    stderr = proc.stderr.read().decode(errors="replace")
    print("--- device_sim stderr ---")
    print(stderr.rstrip())
    print("--------------------------")

    # Device-side result (from the C++ harness self-checks)
    check(rc == 0, "device_sim exit code 0 (all client-side checks passed)")
    check("RESULT PASS" in stderr, "device_sim reports RESULT PASS")

    # Hub-side observations (from the Python agent that talked to it)
    check(proto.hello_count >= 1, "hub received HELLO handshake")
    check(proto.config_requests >= 1, "hub served a CONFIG request")
    check(proto.naks_sent == 1, "hub NAKed exactly one MEASURES frame")
    check(proto.measures_seen == 4, "hub saw 4 MEASURES frames (3 posts + 1 retransmit)")
    check(len(proto.measures) == 3, "hub ACKed and recorded 3 measurements")

    # Wire integrity across the whole exchange (device -> hub direction)
    check(reader.crc_errors == 0, "no CRC errors on device->hub frames")
    check(reader.frames_ok >= 6, "hub parsed all device frames cleanly")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    rc = run()
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(rc if rc == 2 else (0 if failed == 0 else 1))
