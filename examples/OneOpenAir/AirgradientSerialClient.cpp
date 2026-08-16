/*
 * AirgradientSerialClient.cpp
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#include "AirgradientSerialClient.h"

namespace WT = agwire::WireType;

AirgradientSerialClient::AirgradientSerialClient(AgWireLink &link) : link_(link) {}

bool AirgradientSerialClient::handshake() {
  // {"sn":"...","fw":"...","model":"...","proto":1}
  std::string hello = "{\"sn\":\"" + serialNumber +
                      "\",\"proto\":1,\"transport\":\"usb-cdc\"}";
  for (int i = 0; i < helloAttempts_; i++) {
    link_.send(WT::Hello, hello);
    std::string ready;
    if (link_.waitForType(WT::Ready, helloTimeoutMs_, ready)) {
      setClientReady(true);
      registeredOnAgServer = true;
      return true;
    }
  }
  setClientReady(false);
  return false;
}

bool AirgradientSerialClient::begin(std::string sn, PayloadType pt) {
  serialNumber = sn;
  payloadType = pt;
  // Attempt handshake but never hard-fail: the Sensor Hub may come up slightly
  // after the monitor. networkingTask re-runs ensureClientConnection() later.
  handshake();
  return true;
}

std::string AirgradientSerialClient::httpFetchConfig() {
  std::string req = "{\"sn\":\"" + serialNumber + "\"}";
  link_.send(WT::GetConfig, req);

  std::string body;
  if (!link_.waitForType(WT::Config, cfgTimeoutMs_, body)) {
    lastFetchConfigSucceed = false;
    return {};
  }
  if (body.empty()) {
    lastFetchConfigSucceed = false;
    return body;
  }
  registeredOnAgServer = true;
  lastFetchConfigSucceed = true;
  return body; // caller (Configuration::parse) validates it
}

bool AirgradientSerialClient::httpPostMeasures(const std::string &payload) {
  for (int attempt = 0; attempt <= maxRetries_; attempt++) {
    uint32_t seq = link_.send(WT::Measures, payload);
    bool acked = false;
    if (link_.waitForAck(seq, ackTimeoutMs_, acked)) {
      if (acked) {
        lastPostMeasuresSucceed = true;
        setClientReady(true);
        return true;
      }
      // NAK: hub asked for a resend -> retry immediately
      continue;
    }
    // timeout -> retry
  }
  lastPostMeasuresSucceed = false;
  setClientReady(false); // triggers re-handshake in networkingTask
  return false;
}

bool AirgradientSerialClient::ensureClientConnection(bool reset) {
  (void)reset;
  return handshake();
}
