#!/usr/bin/env python3
"""ag_wired_hub.py -- reference AirGradient Sensor Hub agent (Path B, host side).

Runs on the Sensor Hub. Powers + reads an AirGradient monitor over one USB-C
cable (5V power + USB-CDC data). Speaks the wired protocol:

    device -> hub : HELLO (H), MEASURES (M), GETCONFIG (G), DATA (D, one-way)
    hub -> device : READY (Y), ACK (K), NAK (N), CONFIG (C)

HubProtocol is transport-agnostic and importable, so the same logic can be
unit-tested and driven over a serial port, a pipe, or stdin/stdout.

Usage:
    python3 ag_wired_hub.py --port /dev/ttyACM0        # real device over USB-CDC
    python3 ag_wired_hub.py --stdio                    # bytes over stdin/stdout
"""
import argparse
import json
import sys
import time

import ag_wire as W


# A plausible AirGradient config body served on GETCONFIG. The device's
# Configuration::parse() consumes this exactly like the cloud /config response.
DEFAULT_CONFIG = {
    "country": "US",
    "pmStandard": "ugm3",
    "ledBarMode": "co2",
    "abcDays": 30,
    "tvocLearningOffset": 12,
    "noxLearningOffset": 12,
    "temperatureUnit": "c",
    "postDataToAirGradient": True,
    "ledBarBrightness": 100,
    "displayBrightness": 100,
    "model": "I-9PSL",
}


class HubProtocol:
    """Transport-agnostic hub logic. Feed device frames, get response frames."""

    def __init__(self, config=None, nak_first=0, on_measure=None, log=None):
        self.config = config if config is not None else dict(DEFAULT_CONFIG)
        # NAK the first `nak_first` MEASURES frames (to exercise device retransmit)
        self._nak_remaining = int(nak_first)
        self.on_measure = on_measure
        self.log = log or (lambda *a: None)
        self.hello_count = 0
        self.measures = []       # list of (seq, dict) that were ACKed
        self.measures_seen = 0   # total MEASURES frames (incl. NAKed)
        self.naks_sent = 0
        self.config_requests = 0

    def handle(self, frame: "W.Frame"):
        """Return a list of response frames (bytes) for one inbound frame."""
        t = frame.type
        if t == W.HELLO:
            self.hello_count += 1
            self.log("HELLO", frame.payload)
            return [W.build_frame(W.READY, frame.seq, json.dumps({"proto": 1}))]

        if t == W.MEASURES:
            self.measures_seen += 1
            if self._nak_remaining > 0:
                self._nak_remaining -= 1
                self.naks_sent += 1
                self.log("MEASURES seq=%d -> NAK (test)" % frame.seq)
                return [W.build_frame(W.NAK, frame.seq, str(frame.seq))]
            rec = self._json(frame.payload)
            self.measures.append((frame.seq, rec))
            if self.on_measure:
                self.on_measure(frame.seq, rec)
            self.log("MEASURES seq=%d -> ACK" % frame.seq)
            return [W.build_frame(W.ACK, frame.seq, str(frame.seq))]

        if t == W.GETCONFIG:
            self.config_requests += 1
            self.log("GETCONFIG -> CONFIG")
            return [W.build_frame(W.CONFIG, frame.seq, json.dumps(self.config))]

        if t == W.DATA:
            # Path A one-way stream: record, no reply.
            self.measures.append((frame.seq, self._json(frame.payload)))
            self.log("DATA seq=%d (one-way)" % frame.seq)
            return []

        self.log("unhandled frame type %r" % t)
        return []

    @staticmethod
    def _json(payload: bytes):
        try:
            return json.loads(payload.decode())
        except Exception:
            return {"_raw": payload.decode(errors="replace")}


def _run_stream(read_fn, write_fn, proto: HubProtocol, poll=0.005):
    reader = W.FrameReader()
    while True:
        chunk = read_fn()
        if chunk:
            for frame in reader.feed(chunk):
                for resp in proto.handle(frame):
                    write_fn(resp)
        else:
            time.sleep(poll)


def main(argv=None):
    ap = argparse.ArgumentParser(description="AirGradient Sensor Hub agent (Path B)")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port, e.g. /dev/ttyACM0")
    src.add_argument("--stdio", action="store_true", help="use stdin/stdout")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    log = (lambda *a: None) if args.quiet else (lambda *a: print("[hub]", *a, file=sys.stderr))
    proto = HubProtocol(log=log)

    if args.stdio:
        stdin = sys.stdin.buffer
        stdout = sys.stdout.buffer
        import os
        os.set_blocking(stdin.fileno(), False)

        def rd():
            return stdin.read(256) or b""

        def wr(b):
            stdout.write(b)
            stdout.flush()

        _run_stream(rd, wr, proto)
        return 0

    try:
        import serial
    except ImportError:
        print("pyserial required: pip install pyserial", file=sys.stderr)
        return 2
    ser = serial.Serial(args.port, args.baud, timeout=0)
    log("Sensor Hub agent listening on %s" % args.port)

    def rd():
        return ser.read(256)

    def wr(b):
        ser.write(b)

    _run_stream(rd, wr, proto)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
