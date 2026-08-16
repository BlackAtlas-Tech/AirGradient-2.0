#!/usr/bin/env python3
"""
ag_wired_reader.py -- host-side reader for the AirGradient Path A wired stream.

Reads framed measurement lines from the monitor's USB-C (USB-CDC) port and
prints the decoded JSON. Debug-log lines sharing the same port are ignored;
CRC-failed frames are dropped and counted.

Framing/CRC live in ag_wire.py (shared with the firmware's AgWireFrame.h and the
Path B Sensor Hub agent), so there is one source of truth for the wire format.

Frame:  $AG<T>,<seq>,<len>|<payload>*<CRC16>\\r\\n   (CRC-16/XMODEM over payload)

For the bidirectional Path B link to a Sensor Hub, use ag_wired_hub.py instead.

Usage:
    python3 ag_wired_reader.py --port /dev/ttyACM0 --baud 115200
    python3 ag_wired_reader.py --port COM5 --json-only
    cat capture.bin | python3 ag_wired_reader.py --stdin      # replay a capture

Only the serial mode needs pyserial:  pip install pyserial
"""
import argparse
import json
import sys

import ag_wire as W

# Back-compat re-export for tests/tools that imported this from the reader.
crc16_xmodem = W.crc16_xmodem


class FrameParser:
    """Incremental parser adapter over ag_wire.FrameReader.

    feed(chunk) -> list of (seq, payload) tuples. By default every frame type is
    returned (Path A streams are all DATA frames); pass `types` to filter.
    """

    def __init__(self, types=None):
        self._r = W.FrameReader()
        self._types = types  # None = keep all; else a set/str of type chars

    @property
    def frames_ok(self):
        return self._r.frames_ok

    @property
    def crc_errors(self):
        return self._r.crc_errors

    def feed(self, chunk):
        out = []
        for f in self._r.feed(chunk):
            if self._types is None or f.type in self._types:
                out.append((f.seq, f.payload))
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--stdin", action="store_true", help="read raw bytes from stdin")
    ap.add_argument("--json-only", action="store_true",
                    help="print only the JSON payload, one per line")
    args = ap.parse_args()

    parser = FrameParser()

    def handle(frames):
        for seq, payload in frames:
            try:
                obj = json.loads(payload)
            except json.JSONDecodeError:
                obj = {"_raw": payload.decode("utf-8", "replace")}
            if args.json_only:
                print(json.dumps(obj, separators=(",", ":")))
            else:
                print(f"[seq {seq}] {json.dumps(obj)}")
            sys.stdout.flush()

    if args.stdin:
        handle(parser.feed(sys.stdin.buffer.read()))
    elif args.port:
        try:
            import serial  # pyserial
        except ImportError:
            sys.exit("pyserial not installed: pip install pyserial")
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            while True:
                chunk = ser.read(256)
                if chunk:
                    handle(parser.feed(chunk))
    else:
        ap.error("need --port or --stdin")

    sys.stderr.write(f"# frames_ok={parser.frames_ok} crc_errors={parser.crc_errors}\n")


if __name__ == "__main__":
    main()
