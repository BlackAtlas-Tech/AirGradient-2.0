// Reproduces the exact bytes WiredEmitter::emit() writes to Serial, using the
// same agwire::buildFrame() the firmware calls. Interleaves debug-log lines the
// way the real device does on the shared USB-CDC port. Output -> stdout.
#include "AgWireFrame.h"
#include <cstdio>
#include <string>

int main() {
  const char *payloads[] = {
      "{\"pm01\":2,\"pm02\":5,\"pm10\":7,\"atmp\":23.4,\"rhum\":41.2,\"rco2\":812,"
      "\"tvocIndex\":102,\"wifi\":-58,\"boot\":3,\"bootCount\":3,\"ledMode\":\"co2\","
      "\"serialno\":\"ecda3b1eaaaf\",\"firmware\":\"3.6.5\",\"model\":\"I-9PSL\"}",
      "{\"pm02\":6,\"atmp\":23.5,\"rhum\":40.9,\"rco2\":False}", // odd value, still valid frame
      "{\"pm02\":4,\"atmp\":23.6,\"rhum\":41.0,\"rco2\":790,\"wifi\":-60}",
  };

  // boot-time debug chatter (like Serial.println in setup)
  std::fputs("Detected AirGradient ONE\r\n", stdout);
  std::fputs("Serial nr: ecda3b1eaaaf\r\n", stdout);
  std::fputs("Wired emitter enabled, interval 10000 ms\r\n", stdout);

  uint32_t seq = 0;
  for (int i = 0; i < 3; ++i) {
    // periodic debug line the firmware also prints (printMeasurements)
    std::fputs("[   4521][I] measurements: co2=812 pm25=5\r\n", stdout);
    std::string frame = agwire::buildFrame(seq++, payloads[i]);
    std::fwrite(frame.data(), 1, frame.size(), stdout);
  }
  return 0;
}
