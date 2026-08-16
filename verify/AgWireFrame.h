/*
 * AgWireFrame.h  --  Wired (USB-CDC) framing for AirGradient Path A + Path B
 *
 * Dependency-free (no Arduino, no ESP-IDF): pure C++ so the *exact* same
 * framing + checksum + parser compile on the device AND in host tooling/tests.
 *
 * Frame (one line):
 *
 *     $AG<T>,<seq>,<len>|<payload>*<CRC16>\r\n
 *
 *   $AG      3-byte literal sentinel.
 *   <T>      1 uppercase letter = frame type (see WireType below).
 *            Path A data frames use 'D', so "$AGD," is unchanged.
 *   <seq>    decimal uint32 sequence number.
 *   <len>    decimal byte length of <payload>.  Length-prefix framing means the
 *            payload may contain ',', ':', '*', '|' or a stray newline safely.
 *   <payload> opaque bytes (JSON for D/M/G/C/Y, decimal seq text for K/N).
 *   <CRC16>  CRC-16/XMODEM over <payload>, 4 uppercase hex chars.
 *   \r\n     terminator.
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#ifndef AG_WIRE_FRAME_H
#define AG_WIRE_FRAME_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace agwire {

/** Frame type characters. Device->Hub: H,M,G,D  Hub->Device: Y,K,N,C */
namespace WireType {
constexpr char Hello = 'H';   // D->H handshake        payload: {"sn","fw","model","proto"}
constexpr char Ready = 'Y';   // H->D handshake reply  payload: {"proto":1,...}
constexpr char Measures = 'M';// D->H post measures    payload: measurement JSON
constexpr char Ack = 'K';     // H->D ack of an M      payload: decimal acked seq
constexpr char Nak = 'N';     // H->D nak of an M      payload: decimal nakked seq
constexpr char GetConfig = 'G';// D->H config request  payload: {"sn":...}
constexpr char Config = 'C';  // H->D config reply     payload: config JSON body
constexpr char Data = 'D';    // D->H one-way stream (Path A)  payload: measurement JSON
} // namespace WireType

/** CRC-16/XMODEM. Reference vector: crc16("123456789") == 0x31C3. */
inline uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

/** Build a complete framed line of the given type. */
inline std::string buildFrame(char type, uint32_t seq, const std::string &payload) {
  const uint16_t c =
      crc16(reinterpret_cast<const uint8_t *>(payload.data()), payload.size());

  char header[40];
  const int hn = std::snprintf(header, sizeof(header), "$AG%c,%lu,%lu|", type,
                               static_cast<unsigned long>(seq),
                               static_cast<unsigned long>(payload.size()));

  char tail[16];
  const int tn = std::snprintf(tail, sizeof(tail), "*%04X\r\n", c);

  std::string out;
  out.reserve(static_cast<size_t>(hn) + payload.size() + static_cast<size_t>(tn));
  out.append(header, static_cast<size_t>(hn));
  out.append(payload);
  out.append(tail, static_cast<size_t>(tn));
  return out;
}

/** Path A convenience: a Data ('D') frame. */
inline std::string buildFrame(uint32_t seq, const std::string &payload) {
  return buildFrame(WireType::Data, seq, payload);
}

struct Frame {
  char type = 0;
  uint32_t seq = 0;
  std::string payload;
};

/*
 * Incremental byte-stream parser. Feed bytes, get back validated frames.
 * Robust to interleaved debug text, frames split across reads, garbage
 * between frames, and CRC-corrupted frames (dropped + counted). Resyncs by
 * scanning forward to the next "$AG" on any structural failure.
 *
 * This mirrors the host reader (ag_wired_reader.py) exactly so both ends agree.
 */
class FrameReader {
public:
  uint32_t framesOk = 0;
  uint32_t crcErrors = 0;

  std::vector<Frame> feed(const uint8_t *data, size_t n) {
    buf_.append(reinterpret_cast<const char *>(data), n);
    std::vector<Frame> out;
    Frame f;
    while (extractOne(f)) {
      out.push_back(std::move(f));
      f = Frame();
    }
    // Bound growth if we're mid-garbage with no sentinel in sight.
    if (buf_.find(kPfx) == std::string::npos && buf_.size() > 4096) {
      buf_.erase(0, buf_.size() - (sizeof(kPfx) - 1));
    }
    return out;
  }

private:
  static constexpr const char *kPfx = "$AG";
  std::string buf_;

  void resyncAfter(size_t from) {
    size_t nxt = buf_.find(kPfx, from);
    if (nxt == std::string::npos) {
      buf_.clear();
    } else {
      buf_.erase(0, nxt);
    }
  }

  // Returns true and fills `out` when a complete valid frame was consumed.
  bool extractOne(Frame &out) {
    size_t start = buf_.find(kPfx);
    if (start == std::string::npos) {
      return false;
    }
    if (start > 0) {
      buf_.erase(0, start); // drop leading noise (debug lines etc.)
    }
    // Need "$AG" + type + ','  -> at least 5 bytes.
    if (buf_.size() < 5) {
      return false;
    }
    char type = buf_[3];
    if (type < 'A' || type > 'Z' || buf_[4] != ',') {
      resyncAfter(1);
      return extractOne(out);
    }
    size_t bar = buf_.find('|', 5);
    if (bar == std::string::npos) {
      if (buf_.size() > 64) { // header must be short; junk -> resync
        resyncAfter(1);
        return extractOne(out);
      }
      return false; // wait for more
    }
    // header body between index 5 and bar == "<seq>,<len>"
    std::string hdr = buf_.substr(5, bar - 5);
    size_t comma = hdr.find(',');
    if (comma == std::string::npos) {
      resyncAfter(1);
      return extractOne(out);
    }
    uint32_t seq = 0, plen = 0;
    if (!parseU32(hdr.substr(0, comma), seq) ||
        !parseU32(hdr.substr(comma + 1), plen)) {
      resyncAfter(1);
      return extractOne(out);
    }
    size_t need = bar + 1 + plen + 1 + 4; // payload + '*' + 4 hex
    if (buf_.size() < need) {
      return false; // wait for the rest of the frame
    }
    if (buf_[bar + 1 + plen] != '*') {
      resyncAfter(1);
      return extractOne(out);
    }
    std::string payload = buf_.substr(bar + 1, plen);
    std::string crchex = buf_.substr(bar + 2 + plen, 4);

    size_t end = bar + 6 + plen;
    while (end < buf_.size() && (buf_[end] == '\r' || buf_[end] == '\n')) {
      end++;
    }
    buf_.erase(0, end);

    uint16_t got = 0;
    if (!parseHex16(crchex, got) ||
        got != crc16(reinterpret_cast<const uint8_t *>(payload.data()),
                     payload.size())) {
      crcErrors++;
      return extractOne(out);
    }
    framesOk++;
    out.type = type;
    out.seq = seq;
    out.payload = std::move(payload);
    return true;
  }

  static bool parseU32(const std::string &s, uint32_t &v) {
    if (s.empty()) return false;
    uint64_t acc = 0;
    for (char ch : s) {
      if (ch < '0' || ch > '9') return false;
      acc = acc * 10 + (ch - '0');
      if (acc > 0xFFFFFFFFULL) return false;
    }
    v = static_cast<uint32_t>(acc);
    return true;
  }
  static bool parseHex16(const std::string &s, uint16_t &v) {
    if (s.size() != 4) return false;
    uint16_t acc = 0;
    for (char ch : s) {
      acc <<= 4;
      if (ch >= '0' && ch <= '9') acc |= (ch - '0');
      else if (ch >= 'A' && ch <= 'F') acc |= (ch - 'A' + 10);
      else if (ch >= 'a' && ch <= 'f') acc |= (ch - 'a' + 10);
      else return false;
    }
    v = acc;
    return true;
  }
};

} // namespace agwire

#endif // AG_WIRE_FRAME_H
