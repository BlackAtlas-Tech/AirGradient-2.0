/*
 * WiredEmitter.cpp
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#include "WiredEmitter.h"
#include "AgWireFrame.h"
#include <string>

WiredEmitter::WiredEmitter(Stream &out, Measurements &measures)
    : out_(out), measures_(measures), fwMode_(FW_MODE_I_9PSL), seq_(0) {}

void WiredEmitter::begin(AgFirmwareMode fwMode) { fwMode_ = fwMode; }

size_t WiredEmitter::emit(int rssi) {
  // localServer=true -> same schema as GET /measures/current (adds serialno,
  // firmware, model, ledMode) so a wired host sees an identical payload.
  String json = measures_.toString(true, fwMode_, rssi);

  std::string payload(json.c_str(), json.length());
  std::string frame = agwire::buildFrame(seq_++, payload);

  return out_.write(reinterpret_cast<const uint8_t *>(frame.data()),
                    frame.size());
}
