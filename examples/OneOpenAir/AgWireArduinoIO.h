/*
 * AgWireArduinoIO.h  --  Arduino adapter: binds AgWireLink to a Stream.
 *
 * This is the ONLY Arduino-dependent part of the Path B stack. Everything above
 * it (AgWireLink, AirgradientSerialClient) is portable C++.
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#ifndef AG_WIRE_ARDUINO_IO_H
#define AG_WIRE_ARDUINO_IO_H

#include "AgWireLink.h"
#include <Arduino.h>

class StreamByteIO : public IByteIO {
public:
  explicit StreamByteIO(Stream &s) : s_(s) {}

  int readByte() override { return s_.read(); }

  size_t writeBytes(const uint8_t *data, size_t n) override {
    return s_.write(data, n);
  }

  uint32_t nowMs() override { return millis(); }

  // delay() yields to FreeRTOS (feeds the idle/watchdog tasks), so short waits
  // in the networking task don't starve the system.
  void sleepMs(uint32_t ms) override { delay(ms); }

private:
  Stream &s_;
};

#endif // AG_WIRE_ARDUINO_IO_H
