/*
 * AgWireLink.cpp
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */
#include "AgWireLink.h"

AgWireLink::AgWireLink(IByteIO &io) : io_(io) {}

uint32_t AgWireLink::send(char type, const std::string &payload) {
  uint32_t s = seq_++;
  std::string frame = agwire::buildFrame(type, s, payload);
  io_.writeBytes(reinterpret_cast<const uint8_t *>(frame.data()), frame.size());
  return s;
}

void AgWireLink::enqueue(agwire::Frame &&f) {
  if (inbox_.size() >= kMaxInbox) {
    inbox_.pop_front(); // drop oldest to bound memory
  }
  inbox_.push_back(std::move(f));
}

void AgWireLink::pump() {
  uint8_t chunk[64];
  size_t n = 0;
  int b;
  while (n < sizeof(chunk) && (b = io_.readByte()) >= 0) {
    chunk[n++] = static_cast<uint8_t>(b);
  }
  if (n == 0) {
    return;
  }
  auto frames = reader_.feed(chunk, n);
  for (auto &f : frames) {
    enqueue(std::move(f));
  }
}

bool AgWireLink::waitForType(char type, uint32_t timeoutMs, std::string &outPayload) {
  uint32_t start = io_.nowMs();
  for (;;) {
    pump();
    for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
      if (it->type == type) {
        outPayload = std::move(it->payload);
        inbox_.erase(it);
        return true;
      }
    }
    if ((io_.nowMs() - start) >= timeoutMs) {
      return false;
    }
    io_.sleepMs(2);
  }
}

bool AgWireLink::waitForAck(uint32_t seq, uint32_t timeoutMs, bool &acked) {
  uint32_t start = io_.nowMs();
  for (;;) {
    pump();
    for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
      if (it->type == agwire::WireType::Ack || it->type == agwire::WireType::Nak) {
        // payload is the acked/nakked seq as decimal text
        uint32_t got = 0;
        bool ok = !it->payload.empty();
        for (char ch : it->payload) {
          if (ch < '0' || ch > '9') { ok = false; break; }
          got = got * 10 + (ch - '0');
        }
        if (ok && got == seq) {
          acked = (it->type == agwire::WireType::Ack);
          inbox_.erase(it);
          return true;
        }
      }
    }
    if ((io_.nowMs() - start) >= timeoutMs) {
      return false;
    }
    io_.sleepMs(2);
  }
}
