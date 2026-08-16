/*
 * AirgradientSerialClient.h  --  Path B wired transport.
 *
 * A drop-in AirgradientClient whose "network" is the USB-C link to a host
 * Sensor Hub. Pure C++ (no Arduino) so it can be integration-tested on a host
 * against the reference hub agent.
 *
 *   begin()               -> HELLO handshake, wait READY
 *   httpFetchConfig()     -> GETCONFIG request, wait CONFIG reply
 *   httpPostMeasures()    -> MEASURES post, wait ACK (retry on NAK/timeout)
 *   ensureClientConnection() -> re-handshake
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#ifndef AIRGRADIENT_SERIAL_CLIENT_H
#define AIRGRADIENT_SERIAL_CLIENT_H

#include "AgWireLink.h"
#include "Libraries/airgradient-client/src/airgradientClient.h"
#include <string>

class AirgradientSerialClient : public AirgradientClient {
public:
  explicit AirgradientSerialClient(AgWireLink &link);
  ~AirgradientSerialClient() override {}

  bool begin(std::string sn, PayloadType pt) override;
  std::string httpFetchConfig() override;
  bool httpPostMeasures(const std::string &payload) override;
  bool ensureClientConnection(bool reset) override;

  // Tunables (ms / counts)
  void setAckTimeoutMs(uint32_t v) { ackTimeoutMs_ = v; }
  void setConfigTimeoutMs(uint32_t v) { cfgTimeoutMs_ = v; }
  void setHelloTimeoutMs(uint32_t v) { helloTimeoutMs_ = v; }
  void setMaxRetries(int v) { maxRetries_ = v; }

private:
  AgWireLink &link_;
  uint32_t ackTimeoutMs_ = 3000;
  uint32_t cfgTimeoutMs_ = 5000;
  uint32_t helloTimeoutMs_ = 2000;
  int maxRetries_ = 3;
  int helloAttempts_ = 3;

  bool handshake();
};

#endif // AIRGRADIENT_SERIAL_CLIENT_H
