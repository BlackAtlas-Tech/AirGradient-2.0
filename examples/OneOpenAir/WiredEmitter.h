/*
 * WiredEmitter.h  --  Path A: stream measurements out the USB-C port.
 *
 * Reuses Measurements::toString() (the same JSON served at /measures/current
 * and published over MQTT) and pushes it, framed, to any Stream -- normally
 * the ESP32-C3 native USB-CDC port (`Serial`). Framing lets the data coexist
 * on the same physical port as the human-readable debug log.
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#ifndef WIRED_EMITTER_H
#define WIRED_EMITTER_H

#include "AgValue.h"
#include "App/AppDef.h"
#include <Arduino.h>

class WiredEmitter {
public:
  WiredEmitter(Stream &out, Measurements &measures);

  /** Latch the firmware mode used when serializing the payload. */
  void begin(AgFirmwareMode fwMode);

  /** Build and write one framed measurement. `rssi` mirrors the value the
   *  local HTTP server passes to toString(). Returns bytes written. */
  size_t emit(int rssi);

  uint32_t framesSent() const { return seq_; }

private:
  Stream &out_;
  Measurements &measures_;
  AgFirmwareMode fwMode_;
  uint32_t seq_;
};

#endif // WIRED_EMITTER_H
