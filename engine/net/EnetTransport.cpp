#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // enet.h brings in windows.h, whose min/max macros break std::max
#endif
#endif
#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <random>
#include <unordered_map>

#include "../core/logger/logging.hpp"
#include "Transport.hpp"
#include "Wire.hpp"

namespace net {
namespace {

// enet_initialize/deinitialize, once for the process however many transports there are.
struct EnetLibrary {
  EnetLibrary() { ok = enet_initialize() == 0; }
  ~EnetLibrary() {
    if (ok) enet_deinitialize();
  }
  bool ok = false;
};

bool parseAddress(const std::string& text, ENetAddress& out) {
  const auto colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) return false;
  const std::string host = text.substr(0, colon);
  int port = 0;
  for (char c : text.substr(colon + 1)) {
    if (c < '0' || c > '9') return false;
    port = port * 10 + (c - '0');
    if (port > 65535) return false;
  }
  if (enet_address_set_host(&out, host.c_str()) != 0) return false;
  out.port = static_cast<enet_uint16>(port);
  return true;
}

std::string formatAddress(const ENetAddress& address) {
  char ip[64] = {};
  if (enet_address_get_host_ip(&address, ip, sizeof(ip)) != 0) return {};
  return std::string(ip) + ":" + std::to_string(address.port);
}

class EnetTransport final : public Transport {
 public:
  ~EnetTransport() override { close(); }

  bool open(uint16_t port, size_t maxConnections) override {
    static EnetLibrary library;
    if (!library.ok) {
      JM_LOG_ERROR("[Net] ENet failed to initialize");
      return false;
    }
    if (_host && (port == 0 || port == _port)) return true;
    close();
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    _host = enet_host_create(&address, std::max<size_t>(maxConnections, 1), kChannelCount, 0, 0);
    if (!_host) {
      JM_LOG_ERROR("[Net] can't open UDP port {} (in use?)", port);
      return false;
    }
    _host->maximumPacketSize = kMaxPacket;  // bigger ones from a peer are dropped, not reassembled
    ENetAddress bound{};
    _port = enet_socket_get_address(_host->socket, &bound) == 0 ? bound.port : port;
    JM_LOG_INFO("[Net] UDP port {} open", _port);
    return true;
  }

  void close() override {
    if (!_host) return;
    for (auto& [id, peer] : _peers) enet_peer_disconnect_now(peer, 0);
    enet_host_flush(_host);
    enet_host_destroy(_host);
    _host = nullptr;
    _peers.clear();
    _delayed.clear();
    _port = 0;
  }

  bool isOpen() const override { return _host != nullptr; }
  uint16_t port() const override { return _port; }

  ConnId connect(const std::string& text) override {
    ENetAddress address{};
    if (!_host || !parseAddress(text, address)) return 0;
    ENetPeer* peer = enet_host_connect(_host, &address, kChannelCount, 0);
    if (!peer) return 0;
    return adopt(peer);
  }

  void send(ConnId conn, uint8_t channel, const std::vector<uint8_t>& data) override {
    if (_conditions.active()) {
      if (channel == kUnreliable && _conditions.loss > 0.0f && _chance(_rng) < _conditions.loss) return;
      // Jitter only reorders what may arrive out of order anyway.
      const double jitter = channel == kUnreliable && _conditions.jitter > 0.0f ? _chance(_rng) * _conditions.jitter : 0.0;
      _delayed.push_back({now() + _conditions.latency + jitter, conn, channel, data});
      return;
    }
    sendNow(conn, channel, data);
  }

  void disconnect(ConnId conn) override {
    if (auto it = _peers.find(conn); it != _peers.end()) enet_peer_disconnect_later(it->second, 0);
  }

  void poll(std::vector<TransportEvent>& out) override {
    if (!_host) return;
    // Held messages whose time has come, in the order they were sent per channel.
    if (!_delayed.empty()) {
      const double t = now();
      std::stable_sort(_delayed.begin(), _delayed.end(), [](const Held& a, const Held& b) { return a.due < b.due; });
      while (!_delayed.empty() && _delayed.front().due <= t) {
        sendNow(_delayed.front().conn, _delayed.front().channel, _delayed.front().data);
        _delayed.pop_front();
      }
    }
    ENetEvent event;
    while (_host && enet_host_service(_host, &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
          const ConnId id = event.peer->data ? idOf(event.peer) : adopt(event.peer);
          enet_peer_timeout(event.peer, 32, 3000, 8000);
          out.push_back({TransportEvent::Kind::Connected, id, {}});
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          const ConnId id = idOf(event.peer);
          _peers.erase(id);
          event.peer->data = nullptr;
          if (id) out.push_back({TransportEvent::Kind::Disconnected, id, {}});
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
          const ConnId id = idOf(event.peer);
          if (id) {
            out.push_back({TransportEvent::Kind::Received, id,
                           std::vector<uint8_t>(event.packet->data, event.packet->data + event.packet->dataLength)});
          }
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_NONE:
          break;
      }
    }
    if (_host) enet_host_flush(_host);
  }

  std::string address(ConnId conn) const override {
    auto it = _peers.find(conn);
    return it == _peers.end() ? std::string() : formatAddress(it->second->address);
  }

  float roundTrip(ConnId conn) const override {
    auto it = _peers.find(conn);
    return it == _peers.end() ? 0.0f : static_cast<float>(it->second->roundTripTime) / 1000.0f;
  }

  float sinceHeard(ConnId conn) const override {
    auto it = _peers.find(conn);
    if (it == _peers.end() || !_host || it->second->lastReceiveTime == 0) return 1e9f;
    const enet_uint32 elapsed = enet_time_get() - it->second->lastReceiveTime;  // wraps right
    return static_cast<float>(elapsed) / 1000.0f;
  }

  void punch(const std::string& text) override {
    ENetAddress address{};
    if (!_host || !parseAddress(text, address)) return;
    // Not an ENet packet: the other side's ENet reads and ignores it.
    static const char kPunch[] = "jm-punch";
    ENetBuffer buffer{};
    buffer.data = const_cast<char*>(kPunch);
    buffer.dataLength = sizeof(kPunch) - 1;
    enet_socket_send(_host->socket, &address, &buffer, 1);
  }

  void setConditions(const Conditions& conditions) override {
    _conditions = conditions;
    _rng.seed(conditions.seed);
  }

 private:
  struct Held {
    double due;
    ConnId conn;
    uint8_t channel;
    std::vector<uint8_t> data;
  };

  ENetHost* _host = nullptr;
  uint16_t _port = 0;
  std::unordered_map<ConnId, ENetPeer*> _peers;
  ConnId _nextId = 1;
  Conditions _conditions;
  std::mt19937_64 _rng{1};
  std::uniform_real_distribution<double> _chance{0.0, 1.0};
  std::deque<Held> _delayed;

  static double now() {
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
  }

  ConnId adopt(ENetPeer* peer) {
    const ConnId id = _nextId++;
    peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
    _peers[id] = peer;
    return id;
  }

  static ConnId idOf(const ENetPeer* peer) { return static_cast<ConnId>(reinterpret_cast<uintptr_t>(peer->data)); }

  void sendNow(ConnId conn, uint8_t channel, const std::vector<uint8_t>& data) {
    auto it = _peers.find(conn);
    if (it == _peers.end() || it->second->state != ENET_PEER_STATE_CONNECTED) return;
    const enet_uint32 flags = channel == kReliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
    if (packet && enet_peer_send(it->second, channel, packet) != 0) enet_packet_destroy(packet);
  }
};

}  // namespace

std::unique_ptr<Transport> makeEnetTransport() { return std::make_unique<EnetTransport>(); }

}  // namespace net
