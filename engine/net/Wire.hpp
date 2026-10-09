#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// The bytes multiplayer sends: little-endian numbers, strings as a u32
// length then UTF-8. A reader past the end reads zeros and empty strings and
// remembers it (ok() false), so a short or garbled packet is dropped whole.
// So does a string over its cap. Floats that aren't finite read as 0.
namespace net {

// Bumped whenever a message's layout changes: peers must match.
constexpr uint16_t kProtocolVersion = 2;

// The most a packet (kMaxPacket) or a string in one (kMaxString: names,
// keys, entity.data values, message texts) may hold. The session store and a
// spawn's overrides, read with str(kMaxPacket), may be as big as the packet.
constexpr size_t kMaxPacket = 1u << 20;
constexpr size_t kMaxString = 64u << 10;

enum class Msg : uint8_t {
  Hello = 1,     // client -> host: protocol, game, version, name
  Welcome,       // host -> client: your id, topology, host id, token, players, session values
  Reject,        // host -> client: why not
  PeerHello,     // peer -> peer (p2p): my id, the session's token
  PlayerJoin,    // host -> all: id, name, address
  PlayerLeave,   // host -> all: id
  Scene,         // host -> all: epoch, path
  Group,         // host -> all: epoch, group, spawned?
  Bind,          // host -> all: epoch, [scene entry key, net id]
  Sync,          // host -> all: epoch, key prefix: entries under it the host lacks are gone
  Spawn,         // owner -> all: net id, owner, controller, prefab, x, y, overrides, fields
  Destroy,       // owner -> all: net id
  State,         // owner -> all, unreliable: origin player, time, entities' fields
  Data,          // owner -> all: net id, key, value (entity.data)
  Tags,          // owner -> all: net id, the entity's tags
  Session,       // host -> all: key, value (GameState)
  Input,         // player -> host: player, input snapshot
  Message,       // anyone -> someone: from player and entity, to player or entity, everywhere?, name, text, number
  HostCheck,     // peer -> peer (p2p): lost my link to the host (its id); do you still hear it?
  HostSeen,      // peer -> peer (p2p): the answer, u8
};

// ENet channels: control messages in order; state as it comes (newest wins).
constexpr uint8_t kReliable = 0;
constexpr uint8_t kUnreliable = 1;
constexpr uint8_t kChannelCount = 2;

class Writer {
 public:
  explicit Writer(Msg type) { u8(static_cast<uint8_t>(type)); }
  Writer& u8(uint8_t v) { return raw(&v, 1); }
  Writer& u16(uint16_t v) { return raw(&v, 2); }
  Writer& u32(uint32_t v) { return raw(&v, 4); }
  Writer& i32(int32_t v) { return raw(&v, 4); }
  Writer& f32(float v) { return raw(&v, 4); }
  Writer& f64(double v) { return raw(&v, 8); }
  Writer& str(std::string_view s) {
    u32(static_cast<uint32_t>(s.size()));
    return raw(s.data(), s.size());
  }
  Writer& bytes(const void* data, size_t size) { return raw(data, size); }
  size_t mark() const { return _bytes.size(); }
  void patchU16(size_t at, uint16_t v) { std::memcpy(_bytes.data() + at, &v, 2); }
  const std::vector<uint8_t>& data() const { return _bytes; }
  std::vector<uint8_t>& data() { return _bytes; }
  size_t size() const { return _bytes.size(); }

 private:
  // Little-endian hosts only (every platform the engine ships on).
  Writer& raw(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    _bytes.insert(_bytes.end(), b, b + n);
    return *this;
  }
  std::vector<uint8_t> _bytes;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : _p(data), _end(data + size) {}
  explicit Reader(const std::vector<uint8_t>& bytes) : Reader(bytes.data(), bytes.size()) {}

  uint8_t u8() { return get<uint8_t>(); }
  uint16_t u16() { return get<uint16_t>(); }
  uint32_t u32() { return get<uint32_t>(); }
  int32_t i32() { return get<int32_t>(); }
  float f32() { return finite(get<float>()); }
  double f64() { return finite(get<double>()); }
  std::string str(size_t max = kMaxString) {
    const uint32_t n = u32();
    if (!_ok || n > max || static_cast<size_t>(_end - _p) < n) {
      _ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(_p), n);
    _p += n;
    return s;
  }
  bool read(void* out, size_t n) {
    if (!_ok || static_cast<size_t>(_end - _p) < n) {
      _ok = false;
      std::memset(out, 0, n);
      return false;
    }
    std::memcpy(out, _p, n);
    _p += n;
    return true;
  }
  bool ok() const { return _ok; }
  bool done() const { return _p >= _end; }

 private:
  template <typename T>
  static T finite(T v) {
    return std::isfinite(v) ? v : T{};
  }
  template <typename T>
  T get() {
    T v{};
    read(&v, sizeof(T));
    return v;
  }
  const uint8_t* _p;
  const uint8_t* _end;
  bool _ok = true;
};

}  // namespace net
