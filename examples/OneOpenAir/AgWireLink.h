/*
 * AgWireLink.h  --  Path B device-side link layer over a byte transport.
 *
 * Pure C++ (no Arduino): depends only on the IByteIO abstraction, so the whole
 * link + client stack is host-testable against the reference Sensor Hub agent.
 * The Arduino adapter that binds this to `Serial` lives in AgWireArduinoIO.h.
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#ifndef AG_WIRE_LINK_H
#define AG_WIRE_LINK_H

#include "AgWireFrame.h"
#include <cstdint>
#include <deque>
#include <string>

/** Minimal byte transport + clock the link needs. One impl per platform. */
class IByteIO {
public:
  virtual ~IByteIO() {}
  virtual int readByte() = 0;                            // next byte, or -1 if none
  virtual size_t writeBytes(const uint8_t *data, size_t n) = 0;
  virtual uint32_t nowMs() = 0;                          // monotonic milliseconds
  virtual void sleepMs(uint32_t ms) = 0;                 // yield / cooperative wait
};

class AgWireLink {
public:
  explicit AgWireLink(IByteIO &io);

  /** Send a typed frame with a fresh sequence number; returns the seq used. */
  uint32_t send(char type, const std::string &payload);

  /** Drain available RX bytes into the inbox (non-blocking). */
  void pump();

  /**
   * Wait up to timeoutMs for a frame of `type`; on success copies its payload
   * to `outPayload` and returns true. Other frames received meanwhile are
   * discarded (with a small bounded inbox).
   */
  bool waitForType(char type, uint32_t timeoutMs, std::string &outPayload);

  /**
   * Wait up to timeoutMs for an ACK ('K') or NAK ('N') whose payload equals
   * `seq`. Sets `acked` = true on ACK, false on NAK. Returns true if either
   * arrived; false on timeout.
   */
  bool waitForAck(uint32_t seq, uint32_t timeoutMs, bool &acked);

  // stats passthrough
  uint32_t framesOk() const { return reader_.framesOk; }
  uint32_t crcErrors() const { return reader_.crcErrors; }

private:
  IByteIO &io_;
  agwire::FrameReader reader_;
  std::deque<agwire::Frame> inbox_;
  uint32_t seq_ = 1;

  static constexpr size_t kMaxInbox = 16;
  void enqueue(agwire::Frame &&f);
};

#endif // AG_WIRE_LINK_H
