#!/usr/bin/env python3
"""
test_hub.py -- unit tests for the reference Sensor Hub agent (ag_wired_hub) and
the shared framing (ag_wire), with no hardware and no subprocess.

Run:  python3 test_hub.py   (from the verify/ directory)
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for cand in (os.path.join(HERE, "..", "host"), HERE):
    if os.path.exists(os.path.join(cand, "ag_wire.py")):
        sys.path.insert(0, cand)
        break

import ag_wire as W
from ag_wired_hub import HubProtocol

passed = failed = 0
def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print(f"[FAIL] {msg}")


def parse_one(data):
    r = W.FrameReader()
    frames = r.feed(data)
    assert len(frames) == 1, f"expected 1 frame, got {len(frames)}"
    return frames[0], r


# --- framing round-trips ---
check(W.crc16_xmodem(b"123456789") == 0x31C3, "crc16 XMODEM reference vector")

for t in (W.HELLO, W.READY, W.MEASURES, W.ACK, W.NAK, W.GETCONFIG, W.CONFIG, W.DATA):
    payload = json.dumps({"t": t, "n": 42}).encode()
    f, r = parse_one(W.build_frame(t, 123, payload))
    check(f.type == t and f.seq == 123 and f.payload == payload,
          f"round-trip type {t}")
    check(r.crc_errors == 0, f"no crc error type {t}")

# payload with delimiters
nasty = b'{"note":"a,b|c*d\\r\\n"}'
f, _ = parse_one(W.build_frame(W.MEASURES, 1, nasty))
check(f.payload == nasty, "delimiters in payload survive framing")


# --- hub protocol behavior ---
hub = HubProtocol()

# HELLO -> READY
resp = hub.handle(W.Frame(W.HELLO, 1, b'{"sn":"x","proto":1}'))
check(len(resp) == 1, "HELLO produces one response")
rf, _ = parse_one(resp[0])
check(rf.type == W.READY, "HELLO -> READY")
check(hub.hello_count == 1, "hello_count incremented")

# GETCONFIG -> CONFIG (valid JSON body)
resp = hub.handle(W.Frame(W.GETCONFIG, 2, b'{"sn":"x"}'))
cf, _ = parse_one(resp[0])
check(cf.type == W.CONFIG, "GETCONFIG -> CONFIG")
cfg = json.loads(cf.payload)
check("pmStandard" in cfg and "postDataToAirGradient" in cfg,
      "CONFIG body is a plausible AG config")

# MEASURES -> ACK with acked-seq payload
resp = hub.handle(W.Frame(W.MEASURES, 555, b'{"pm02":5}'))
af, _ = parse_one(resp[0])
check(af.type == W.ACK and af.payload == b"555", "MEASURES -> ACK carries acked seq")
check(hub.measures == [(555, {"pm02": 5})], "measurement recorded")

# NAK path (count-based) forces the device to retransmit
hub2 = HubProtocol(nak_first=1)
r1 = hub2.handle(W.Frame(W.MEASURES, 10, b'{"pm02":1}'))
nf, _ = parse_one(r1[0])
check(nf.type == W.NAK and nf.payload == b"10", "first MEASURES NAKed when nak_first=1")
check(hub2.measures == [], "NAKed measurement not recorded")
r2 = hub2.handle(W.Frame(W.MEASURES, 11, b'{"pm02":1}'))
af2, _ = parse_one(r2[0])
check(af2.type == W.ACK, "subsequent MEASURES ACKed")
check(hub2.measures_seen == 2 and hub2.naks_sent == 1, "seen/nak counters correct")

# DATA (Path A) is recorded with no reply
resp = hub.handle(W.Frame(W.DATA, 77, b'{"pm02":3}'))
check(resp == [], "DATA frame produces no reply (one-way)")

print(f"\n{passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
