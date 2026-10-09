#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Connections to other processes: messages on numbered channels (Wire.hpp's
// reliable and unreliable ones). One socket, kept open across sessions, so
// a matchmaker's view of this machine's address stays valid for peers that
// then connect to it directly.
namespace net {

using ConnId = uint32_t;  // 0 = none

struct TransportEvent {
  enum class Kind { Connected, Disconnected, Received };
  Kind kind;
  ConnId conn = 0;
  std::vector<uint8_t> data;
};

// Simulated network trouble (JM_NET_LATENCY, JM_NET_LOSS): outgoing messages
// are held for `latency` seconds (plus up to `jitter`) and unreliable ones
// dropped with probability `loss`. Seeded, so a run's drops repeat.
struct Conditions {
  float latency = 0.0f;
  float jitter = 0.0f;
  float loss = 0.0f;
  uint64_t seed = 1;
  bool active() const { return latency > 0.0f || jitter > 0.0f || loss > 0.0f; }
};

class Transport {
 public:
  virtual ~Transport() = default;
  // Opens the socket on `port` (0: any free port). Already open: kept if it's
  // on that port (any port, for 0), otherwise reopened there.
  virtual bool open(uint16_t port, size_t maxConnections) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual uint16_t port() const = 0;
  // "host:port"; 0 if it doesn't parse. Connected (or Disconnected) follows.
  virtual ConnId connect(const std::string& address) = 0;
  virtual void send(ConnId conn, uint8_t channel, const std::vector<uint8_t>& data) = 0;
  // A Disconnected event follows once the other side has heard.
  virtual void disconnect(ConnId conn) = 0;
  // Everything since the last poll; sends go out too.
  virtual void poll(std::vector<TransportEvent>& out) = 0;
  // The other side's address as this side sees it ("1.2.3.4:5678").
  virtual std::string address(ConnId conn) const = 0;
  virtual float roundTrip(ConnId conn) const = 0;  // seconds
  // Seconds since anything arrived from the other side (it acknowledges
  // pings twice a second); a lot for an unknown connection.
  virtual float sinceHeard(ConnId conn) const = 0;
  // A few bytes at `address`, so this machine's router lets that address's
  // packets in (NAT hole punching: both sides do it at once).
  virtual void punch(const std::string& address) = 0;
  virtual void setConditions(const Conditions& conditions) = 0;
};

std::unique_ptr<Transport> makeEnetTransport();

}  // namespace net
