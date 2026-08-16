/*
 * device_sim.cpp -- host-side integration harness for the Path B wired client.
 *
 * Links the REAL firmware protocol code (airgradientClient base + AgWireLink +
 * AirgradientSerialClient) and drives it over stdin/stdout, so it can be run
 * against the reference Sensor Hub agent (ag_wired_hub.HubProtocol) for a true
 * cross-language integration test. Nothing here is Arduino-specific except that
 * we substitute an fd-backed IByteIO for the StreamByteIO used on the device.
 *
 *   RX  = fd 0 (stdin)   TX = fd 1 (stdout)   results = fd 2 (stderr)
 *
 * Exit code 0 = all checks passed.
 */
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "AgWireLink.h"
#include "AirgradientSerialClient.h"

class FdByteIO : public IByteIO {
public:
  FdByteIO(int rfd, int wfd) : rfd_(rfd), wfd_(wfd) {
    int fl = fcntl(rfd_, F_GETFL, 0);
    fcntl(rfd_, F_SETFL, fl | O_NONBLOCK);
    t0_ = std::chrono::steady_clock::now();
  }
  int readByte() override {
    unsigned char c;
    ssize_t n = ::read(rfd_, &c, 1);
    return (n == 1) ? static_cast<int>(c) : -1;
  }
  size_t writeBytes(const uint8_t *d, size_t n) override {
    ssize_t w = ::write(wfd_, d, n);
    return (w < 0) ? 0 : static_cast<size_t>(w);
  }
  uint32_t nowMs() override {
    auto dt = std::chrono::steady_clock::now() - t0_;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(dt).count());
  }
  void sleepMs(uint32_t ms) override { usleep(ms * 1000); }

private:
  int rfd_, wfd_;
  std::chrono::steady_clock::time_point t0_;
};

int main() {
  FdByteIO io(0, 1);
  AgWireLink link(io);
  AirgradientSerialClient client(link);
  client.setHelloTimeoutMs(3000);
  client.setConfigTimeoutMs(3000);
  client.setAckTimeoutMs(3000);

  int fails = 0;
  auto check = [&](bool c, const char *m) {
    fprintf(stderr, "[%s] %s\n", c ? "PASS" : "FAIL", m);
    if (!c) fails++;
  };

  bool began = client.begin("ecda3b1eaaaf", AirgradientClient::ONE_OPENAIR);
  check(began, "begin() returns true");
  check(client.isClientReady(), "HELLO handshake -> client ready");
  check(client.isRegisteredOnAgServer(), "registered flag set after handshake");

  std::string cfg = client.httpFetchConfig();
  check(client.isLastFetchConfigSucceed(), "httpFetchConfig succeeded");
  check(!cfg.empty(), "config body non-empty");
  check(cfg.find("pmStandard") != std::string::npos,
        "config body carries expected field");

  const std::string meas = "{\"pm02\":5,\"rco2\":812,\"atmp\":23.4,\"rhum\":41.2}";
  check(client.httpPostMeasures(meas), "post measures #1 acked (with NAK retransmit)");
  check(client.isLastPostMeasureSucceed(), "lastPostMeasuresSucceed set");
  check(client.httpPostMeasures(meas), "post measures #2 acked");
  check(client.httpPostMeasures(meas), "post measures #3 acked");

  fprintf(stderr, "frames_ok=%u crc_errors=%u\n", link.framesOk(), link.crcErrors());
  check(link.crcErrors() == 0, "no CRC errors over the whole session");

  fprintf(stderr, fails ? "RESULT FAIL\n" : "RESULT PASS\n");
  return fails ? 1 : 0;
}
