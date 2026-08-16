// Host-side verification of the SAME AgWireFrame.h the firmware uses.
// Builds with plain g++ -> proves the framing/CRC code is portable and correct.
#include "AgWireFrame.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// ---- independent reference CRC-16/XMODEM (table-free, written differently) ----
static uint16_t ref_crc16_xmodem(const std::string &s) {
  uint16_t crc = 0;
  for (unsigned char ch : s) {
    crc = crc ^ (uint16_t(ch) << 8);
    for (int i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

// ---- a self-contained reference PARSER (mirrors ag_wired_reader.py logic) ----
// Returns payload if a valid frame is found at the start of `buf`, consuming it.
struct Parsed {
  bool ok = false;
  uint32_t seq = 0;
  std::string payload;
  bool crc_valid = false;
};

static Parsed parse_one(const std::string &frame) {
  Parsed p;
  const std::string pfx = "$AGD,";
  if (frame.rfind(pfx, 0) != 0) return p;
  size_t i = pfx.size();
  // seq
  size_t comma = frame.find(',', i);
  if (comma == std::string::npos) return p;
  uint32_t seq = std::stoul(frame.substr(i, comma - i));
  // len
  size_t bar = frame.find('|', comma + 1);
  if (bar == std::string::npos) return p;
  size_t len = std::stoul(frame.substr(comma + 1, bar - (comma + 1)));
  size_t pstart = bar + 1;
  if (pstart + len + 1 > frame.size()) return p; // need payload + '*'
  std::string payload = frame.substr(pstart, len);
  if (frame[pstart + len] != '*') return p;
  std::string crchex = frame.substr(pstart + len + 1, 4);
  uint16_t got = (uint16_t)std::stoul(crchex, nullptr, 16);
  uint16_t want = agwire::crc16((const uint8_t *)payload.data(), payload.size());
  p.ok = true;
  p.seq = seq;
  p.payload = payload;
  p.crc_valid = (got == want);
  return p;
}

int tests = 0, failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    tests++;                                                                   \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      std::printf("  FAIL: %s\n", msg);                                        \
    }                                                                          \
  } while (0)

int main() {
  std::printf("== AgWireFrame verification ==\n");

  // 1. Known CRC test vector: XMODEM("123456789") == 0x31C3
  {
    uint16_t c = agwire::crc16((const uint8_t *)"123456789", 9);
    std::printf("  crc16(\"123456789\") = 0x%04X (expect 0x31C3)\n", c);
    CHECK(c == 0x31C3, "CRC16/XMODEM known vector");
    CHECK(c == ref_crc16_xmodem("123456789"), "CRC matches independent ref");
  }

  // 2. A realistic AirGradient /measures/current style payload.
  const std::string ag_json =
      "{\"pm01\":2,\"pm02\":5,\"pm10\":7,\"pm003Count\":320,\"atmp\":23.4,"
      "\"rhum\":41.2,\"rco2\":812,\"tvocIndex\":102,\"noxIndex\":1,\"wifi\":-58,"
      "\"boot\":3,\"bootCount\":3,\"ledMode\":\"co2\","
      "\"serialno\":\"ecda3b1eaaaf\",\"firmware\":\"3.6.5\",\"model\":\"I-9PSL\"}";

  // 3. Round-trip several payloads, including nasty ones.
  std::vector<std::string> payloads = {
      ag_json,
      "{}",
      "{\"weird\":\"has,comma:colon*star|bar\"}", // delimiters inside payload
      std::string("{\"embedded_newline\":\"a\nb\"}"), // literal newline in payload
      std::string(1500, 'x'),                          // large payload
  };

  uint32_t seq = 0;
  for (const auto &pl : payloads) {
    std::string frame = agwire::buildFrame(seq, pl);
    Parsed p = parse_one(frame);
    CHECK(p.ok, "frame parses");
    CHECK(p.crc_valid, "crc validates on clean frame");
    CHECK(p.seq == seq, "seq round-trips");
    CHECK(p.payload == pl, "payload round-trips byte-for-byte");
    seq++;
  }

  // 4. Corruption is detected: flip one payload byte, CRC must fail.
  {
    std::string frame = agwire::buildFrame(42, ag_json);
    // find payload start (after '|') and flip a byte
    size_t bar = frame.find('|');
    std::string bad = frame;
    bad[bar + 5] ^= 0x01;
    Parsed p = parse_one(bad);
    CHECK(p.ok, "corrupted frame still structurally parses");
    CHECK(!p.crc_valid, "CRC catches single-bit payload corruption");
  }

  // 5. Corruption of the length field -> should fail to parse cleanly.
  {
    std::string frame = agwire::buildFrame(7, "{\"a\":1}");
    // frame looks like $AGD,7,7|{"a":1}*XXXX ; break the len digit
    Parsed p = parse_one("$AGD,7,99|{\"a\":1}*0000\r\n");
    CHECK(!p.ok || !p.crc_valid, "wrong length does not yield a valid frame");
  }

  // 6. Show one real frame on screen for the record.
  std::printf("  sample frame: %s", agwire::buildFrame(1, ag_json).c_str());

  std::printf("\n%d checks, %d failures\n", tests, failures);
  return failures == 0 ? 0 : 1;
}
