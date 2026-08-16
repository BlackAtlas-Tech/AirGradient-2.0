"""ag_wire.py -- shared framing for the AirGradient wired protocol (Path A + B).

Mirrors AgWireFrame.h exactly. Frame:  $AG<T>,<seq>,<len>|<payload>*<CRC16>\\r\\n
"""

# Frame type characters (must match AgWireFrame.h WireType)
HELLO = "H"
READY = "Y"
MEASURES = "M"
ACK = "K"
NAK = "N"
GETCONFIG = "G"
CONFIG = "C"
DATA = "D"

_SENTINEL = b"$AG"


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for ch in data:
        crc ^= ch << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def build_frame(ftype: str, seq: int, payload: bytes) -> bytes:
    if isinstance(payload, str):
        payload = payload.encode()
    c = crc16_xmodem(payload)
    return b"$AG%s,%d,%d|%s*%04X\r\n" % (ftype.encode(), seq, len(payload), payload, c)


class Frame:
    __slots__ = ("type", "seq", "payload")

    def __init__(self, ftype, seq, payload):
        self.type = ftype
        self.seq = seq
        self.payload = payload

    def __repr__(self):
        return "Frame(%s,%d,%r)" % (self.type, self.seq, self.payload)


class FrameReader:
    """Incremental parser. Feed bytes -> list[Frame]. Mirrors AgWireFrame.h::FrameReader.

    Robust to interleaved debug text, split reads, garbage, and CRC-corrupt frames.
    """

    def __init__(self):
        self.buf = bytearray()
        self.frames_ok = 0
        self.crc_errors = 0

    def feed(self, chunk: bytes):
        self.buf.extend(chunk)
        out = []
        while True:
            f = self._extract()
            if f is None:
                break
            out.append(f)
        if _SENTINEL not in self.buf and len(self.buf) > 4096:
            self.buf = self.buf[-len(_SENTINEL):]
        return out

    def _resync_after(self, start):
        nxt = self.buf.find(_SENTINEL, start)
        self.buf = self.buf[nxt:] if nxt != -1 else bytearray()

    def _extract(self):
        start = self.buf.find(_SENTINEL)
        if start == -1:
            return None
        if start > 0:
            del self.buf[:start]
        if len(self.buf) < 5:
            return None
        ftype = chr(self.buf[3])
        if not ("A" <= ftype <= "Z") or self.buf[4] != ord(","):
            self._resync_after(1)
            return self._extract()
        bar = self.buf.find(b"|", 5)
        if bar == -1:
            if len(self.buf) > 64:
                self._resync_after(1)
                return self._extract()
            return None
        try:
            seq_s, len_s = bytes(self.buf[5:bar]).split(b",")
            seq = int(seq_s)
            plen = int(len_s)
        except ValueError:
            self._resync_after(1)
            return self._extract()
        need = bar + 1 + plen + 1 + 4
        if len(self.buf) < need:
            return None
        if self.buf[bar + 1 + plen] != ord("*"):
            self._resync_after(1)
            return self._extract()
        payload = bytes(self.buf[bar + 1: bar + 1 + plen])
        crchex = bytes(self.buf[bar + 2 + plen: bar + 6 + plen])
        end = bar + 6 + plen
        while end < len(self.buf) and self.buf[end] in (0x0D, 0x0A):
            end += 1
        del self.buf[:end]
        try:
            got = int(crchex, 16)
        except ValueError:
            self.crc_errors += 1
            return self._extract()
        if got != crc16_xmodem(payload):
            self.crc_errors += 1
            return self._extract()
        self.frames_ok += 1
        return Frame(ftype, seq, payload)
