/*
 * frame_reader_test.cpp -- tests for the typed FrameReader (device-side RX parse)
 * in AgWireFrame.h. Builds and runs on a host toolchain, no hardware.
 *
 *   g++ -std=c++17 -O2 -I../examples/OneOpenAir frame_reader_test.cpp -o frt && ./frt
 */
#include "AgWireFrame.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace agwire;

static int checks = 0, failures = 0;
#define CHECK(c, msg)                                                          \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      std::printf("  FAIL: %s\n", msg);                                        \
    }                                                                          \
  } while (0)

static std::vector<Frame> feedStr(FrameReader &r, const std::string &s) {
  return r.feed(reinterpret_cast<const uint8_t *>(s.data()), s.size());
}

int main() {
  std::printf("frame_reader_test (typed FrameReader)\n");

  // 1. Typed prefixes are emitted correctly.
  {
    std::string h = buildFrame(WireType::Hello, 1, "{}");
    CHECK(h.rfind("$AGH,", 0) == 0, "HELLO frame starts with $AGH,");
    std::string d = buildFrame(9, "{}"); // Path A convenience
    CHECK(d.rfind("$AGD,", 0) == 0, "Path A buildFrame -> $AGD, (type D)");
  }

  // 2. Round-trip every frame type through the reader.
  {
    struct TC { char type; uint32_t seq; std::string pl; };
    std::vector<TC> cases = {
        {WireType::Hello, 1, "{\"sn\":\"ecda3b1eaaaf\",\"proto\":1}"},
        {WireType::Ready, 2, "{\"proto\":1}"},
        {WireType::Measures, 100000, "{\"pm02\":5,\"rco2\":812}"},
        {WireType::Ack, 100000, "100000"},
        {WireType::Nak, 7, "7"},
        {WireType::GetConfig, 8, "{\"sn\":\"x\"}"},
        {WireType::Config, 9, "{\"pmStandard\":\"ugm3\"}"},
        {WireType::Data, 4294967295u, "{\"a\":1}"},
    };
    FrameReader r;
    std::string stream;
    for (auto &c : cases) stream += buildFrame(c.type, c.seq, c.pl);
    auto got = feedStr(r, stream);
    CHECK(got.size() == cases.size(), "all typed frames parsed");
    bool allMatch = got.size() == cases.size();
    for (size_t i = 0; i < got.size() && i < cases.size(); i++) {
      if (got[i].type != cases[i].type || got[i].seq != cases[i].seq ||
          got[i].payload != cases[i].pl) {
        allMatch = false;
      }
    }
    CHECK(allMatch, "type/seq/payload preserved for every type");
    CHECK(r.crcErrors == 0, "no CRC errors on clean typed stream");
  }

  // 3. Payload containing delimiters survives length-prefix framing.
  {
    FrameReader r;
    std::string nasty = "{\"note\":\"a,b|c*d\\r\\n end\"}";
    auto got = feedStr(r, buildFrame(WireType::Measures, 3, nasty));
    CHECK(got.size() == 1 && got[0].payload == nasty,
          "delimiters in payload do not break framing");
  }

  // 4. Byte-at-a-time reads reassemble.
  {
    FrameReader r;
    std::string stream = buildFrame(WireType::Measures, 11, "{\"x\":1}") +
                         buildFrame(WireType::Config, 12, "{\"y\":2}");
    std::vector<Frame> got;
    for (char ch : stream) {
      auto v = r.feed(reinterpret_cast<const uint8_t *>(&ch), 1);
      for (auto &f : v) got.push_back(f);
    }
    CHECK(got.size() == 2, "frames reassembled from 1-byte reads");
    CHECK(got.size() == 2 && got[0].type == WireType::Measures &&
              got[1].type == WireType::Config,
          "types correct after byte-wise reassembly");
  }

  // 5. Interleaved debug log lines are ignored.
  {
    FrameReader r;
    std::string stream = "I (1234) app: booting sensors\n" +
                         buildFrame(WireType::Measures, 5, "{\"pm02\":9}") +
                         "W (1250) sgp41: warming up\r\n" +
                         buildFrame(WireType::Ack, 5, "5");
    auto got = feedStr(r, stream);
    CHECK(got.size() == 2, "2 frames extracted from debug-interleaved stream");
    CHECK(r.crcErrors == 0, "debug noise raises no CRC errors");
  }

  // 6. A CRC-corrupted frame is dropped and counted; the next frame survives.
  {
    FrameReader r;
    std::string good = buildFrame(WireType::Measures, 20, "{\"a\":1}");
    std::string bad = buildFrame(WireType::Measures, 21, "{\"a\":2}");
    bad[bad.size() - 4] = (bad[bad.size() - 4] == 'A' ? 'B' : 'A'); // flip a CRC hex digit
    std::string good2 = buildFrame(WireType::Measures, 22, "{\"a\":3}");
    auto got = feedStr(r, good + bad + good2);
    CHECK(got.size() == 2, "corrupt frame dropped, 2 good frames pass");
    CHECK(r.crcErrors == 1, "exactly one CRC error counted");
    CHECK(got.size() == 2 && got[0].seq == 20 && got[1].seq == 22,
          "correct frames survive around the corrupt one");
  }

  // 7. Malformed prefix ($AG + bad type/no comma) resyncs to the next real frame.
  {
    FrameReader r;
    std::string stream = "$AG!,garbage not a frame\r\n" +
                         buildFrame(WireType::Config, 40, "{\"ok\":1}");
    auto got = feedStr(r, stream);
    CHECK(got.size() == 1 && got[0].type == WireType::Config && got[0].seq == 40,
          "resync past malformed prefix to a valid frame");
  }

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
