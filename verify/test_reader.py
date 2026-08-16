#!/usr/bin/env python3
"""Unit tests for FrameParser in ag_wired_reader.py (no hardware, no pyserial)."""
import json
import random

from ag_wired_reader import FrameParser, crc16_xmodem


def build_frame(seq: int, payload: bytes) -> bytes:
    c = crc16_xmodem(payload)
    return b"$AGD,%d,%d|%s*%04X\r\n" % (seq, len(payload), payload, c)


AG = json.dumps({
    "pm01": 2, "pm02": 5, "pm10": 7, "atmp": 23.4, "rhum": 41.2,
    "rco2": 812, "tvocIndex": 102, "wifi": -58, "boot": 3,
    "serialno": "ecda3b1eaaaf", "firmware": "3.6.5", "model": "I-9PSL",
}, separators=(",", ":")).encode()

passed = failed = 0


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print("  FAIL:", msg)


# 1. Known CRC vector
check(crc16_xmodem(b"123456789") == 0x31C3, "crc16 XMODEM vector 0x31C3")

# 2. Clean single frame
p = FrameParser()
frames = p.feed(build_frame(0, AG))
check(len(frames) == 1 and frames[0][1] == AG, "single clean frame decodes")
check(p.crc_errors == 0, "no crc errors on clean frame")

# 3. Debug-log noise interleaved with frames (the whole point of framing)
p = FrameParser()
stream = (b"Detected AirGradient ONE\r\n"
          + build_frame(1, AG)
          + b"PMS sensor connected \r\n"
          + b"Firmware Mode: I-9PSL\r\n"
          + build_frame(2, AG)
          + b"[   4521][I][WiFiGeneric.cpp:1234] noise noise\r\n")
frames = p.feed(stream)
check(len(frames) == 2, "2 frames extracted from noisy stream")
check([f[0] for f in frames] == [1, 2], "sequence numbers preserved")
check(p.crc_errors == 0, "debug noise does not raise crc errors")

# 4. Byte-at-a-time delivery (frames split across many reads)
p = FrameParser()
blob = build_frame(10, AG) + build_frame(11, AG)
got = []
for b in blob:
    got.extend(p.feed(bytes([b])))
check(len(got) == 2, "frames reassembled from 1-byte reads")
check(got[0][0] == 10 and got[1][0] == 11, "order preserved across split reads")

# 5. Corrupted payload -> dropped, counted, and recovery continues
p = FrameParser()
good = build_frame(20, AG)
bad = bytearray(build_frame(21, AG))
bar = bad.index(b"|")
bad[bar + 3] ^= 0x01  # flip a payload byte
good2 = build_frame(22, AG)
frames = p.feed(bytes(good) + bytes(bad) + bytes(good2))
seqs = [f[0] for f in frames]
check(20 in seqs and 22 in seqs, "good frames before/after corruption survive")
check(21 not in seqs, "corrupted frame is dropped")
check(p.crc_errors == 1, "exactly one crc error counted")

# 6. Payload containing the delimiters and even an embedded newline
nasty = b'{"s":"a,b:c*d|e","n":"x\ny"}'
p = FrameParser()
frames = p.feed(b"junk\r\n" + build_frame(30, nasty))
check(len(frames) == 1 and frames[0][1] == nasty,
      "length-framing survives delimiters + newline in payload")

# 7. Garbage that merely looks like a prefix -> resync, no crash
p = FrameParser()
frames = p.feed(b"$AGD,not a real frame at all\r\n" + build_frame(40, AG))
check(len(frames) == 1 and frames[0][0] == 40, "false-prefix resyncs to real frame")

# 8. Fuzz: random interleaving of frames and noise
random.seed(1)
p = FrameParser()
expect = []
stream = bytearray()
for i in range(200):
    if random.random() < 0.5:
        payload = json.dumps({"i": i, "v": random.random()}).encode()
        stream += build_frame(i, payload)
        expect.append((i, payload))
    else:
        stream += bytes(random.randint(1, 20)) if random.random() < 0.3 \
            else b"log line %d bla bla\r\n" % i
# feed in random-sized chunks
out = []
idx = 0
while idx < len(stream):
    n = random.randint(1, 37)
    out.extend(p.feed(bytes(stream[idx:idx + n])))
    idx += n
check(out == expect, f"fuzz: recovered {len(out)}/{len(expect)} frames exactly")

print(f"\n{passed} passed, {failed} failed")
raise SystemExit(0 if failed == 0 else 1)
