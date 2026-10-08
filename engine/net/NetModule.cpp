#include "NetModule.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/logger/logging.hpp"
#include "../inputs/InputsModule.hpp"

// Remote players' input arrives through the inputs module.
template <>
struct ModuleTraits<NetModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<InputsTag>;
};

REGISTER_MODULE(NetModule)

using net::Msg;
using net::Reader;
using net::Writer;

namespace {

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

double steadySeconds() {
  using Clock = std::chrono::steady_clock;
  static const auto start = Clock::now();
  return std::chrono::duration<double>(Clock::now() - start).count();
}

const char* roleName(NetModule::Role role) {
  switch (role) {
    case NetModule::Role::Server: return "server";
    case NetModule::Role::Host: return "host";
    case NetModule::Role::Client: return "client";
    default: return "offline";
  }
}

constexpr double kPunchSeconds = 6.0;

}  // namespace

NetModule::NetModule() : _transport(net::makeEnetTransport()) {}
NetModule::~NetModule() = default;

void NetModule::initialize(Engine& app) {
  _app = &app;
  _inputs = app.getModules().find<InputsModule>();
  _config = app.getManifest().net;
  // A dedicated server may run its sessions differently from the players'
  // own (a matchmaker hosting client/server for players who then play p2p,
  // and keep their own scenes meanwhile): net.server's topology, port,
  // maxPlayers and shareScene win there.
  if (app.server()) {
    const nlohmann::json server = _config.value("server", nlohmann::json::object());
    if (server.is_object()) {
      for (const char* key : {"topology", "port", "maxPlayers", "shareScene"}) {
        if (server.contains(key)) _config[key] = server[key];
      }
    }
  }
  _maxPlayers = std::clamp<size_t>(_config.value("maxPlayers", 8), 1, 250);
  _sendInterval = 1.0 / std::clamp(_config.value("sendRate", 30.0), 1.0, 120.0);
  _interpolationDelay = std::clamp(_config.value("interpolationDelay", 0.1), 0.0, 1.0);
  _shareScene = _config.value("shareScene", true);
  if (auto name = env("JM_NET_NAME"); !name.empty()) _name = name;

  // Trouble on purpose, to see a game cope (JM_NET_LATENCY / JITTER in ms, LOSS 0..1).
  net::Conditions conditions;
  conditions.latency = std::strtof(env("JM_NET_LATENCY").c_str(), nullptr) / 1000.0f;
  conditions.jitter = std::strtof(env("JM_NET_JITTER").c_str(), nullptr) / 1000.0f;
  conditions.loss = std::clamp(std::strtof(env("JM_NET_LOSS").c_str(), nullptr), 0.0f, 1.0f);
  conditions.seed = app.getSeeds().seed() ^ 0x6e6574u;  // not next(): that would shift every script's randomness
  _transport->setConditions(conditions);
  if (auto path = env("JM_NET_TRACE"); !path.empty()) {
    _trace.open(path, std::ios::trunc);
    if (!_trace) JM_LOG_ERROR("[Net] JM_NET_TRACE: can't write '{}'", path);
  }

  hookEngine();

  // A dedicated server hosts from the start; JM_NET_HOST / JM_NET_JOIN do the
  // same for a game (tools, `jm run --peers`).
  const auto envPort = static_cast<uint16_t>(std::strtoul(env("JM_NET_PORT").c_str(), nullptr, 10));
  if (app.server()) startServerScripts();
  if (app.server()) {
    if (!host(envPort)) JM_LOG_ERROR("[Net] the server can't host: {}", _error);
  } else if (auto hosting = env("JM_NET_HOST"); !hosting.empty() && hosting != "0") {
    const auto port = static_cast<uint16_t>(std::strtoul(hosting.c_str(), nullptr, 10));
    if (!host(port > 1 ? port : envPort)) JM_LOG_ERROR("[Net] JM_NET_HOST: {}", _error);
  } else if (auto address = env("JM_NET_JOIN"); !address.empty()) {
    if (!join(address)) JM_LOG_ERROR("[Net] JM_NET_JOIN: {}", _error);
  }
  JM_LOG_INFO("[Net] initialized");
}

// What a game adds for its dedicated server (net.server.scripts): scripts
// only the server runs, each on an entity of its own outside every scene, so
// they last as long as the server (a matchmaker, bots, round rules).
void NetModule::startServerScripts() {
  const nlohmann::json server = _config.value("server", nlohmann::json::object());
  const nlohmann::json scripts = server.is_object() ? server.value("scripts", nlohmann::json::array()) : nlohmann::json();
  World& world = _app->getWorld();
  const ComponentInfo* script = world.getComponentRegistry().getInfoByName("ScriptComponent");
  if (!scripts.is_array() || !script) return;
  for (const auto& path : scripts) {
    if (!path.is_string()) continue;
    const EntityId id = world.createEntity();
    world.addTag(id, "server");
    script->addFromJson(world, id, {{"script", path.get<std::string>()}});
    JM_LOG_INFO("[Net] server script {}", path.get<std::string>());
  }
}

void NetModule::shutdown(Engine&) {
  if (online()) leave();
  _transport->close();
  JM_LOG_INFO("[Net] shutdown");
}

// ---- Sessions ---------------------------------------------------------------------

bool NetModule::openSocket(uint16_t port) {
  if (_transport->open(port, _maxPlayers + 8)) return true;
  _error = port ? "can't open UDP port " + std::to_string(port) + " (is something else using it?)"
                : "can't open a UDP socket";
  return false;
}

bool NetModule::host(uint16_t port, Topology topology) {
  if (online()) {
    _error = "already in a session (Net.leave() first)";
    return false;
  }
  _topology = topology != Topology::None ? topology
              : _config.value("topology", std::string("server")) == "p2p" ? Topology::P2P
                                                                          : Topology::ClientServer;
  const auto configured = static_cast<uint16_t>(_config.value("port", 7777));
  // A p2p host keeps the socket a matchmaker saw, if it has one.
  const bool keepSocket = port == 0 && _topology == Topology::P2P && _transport->isOpen();
  if (!keepSocket && !openSocket(port ? port : configured)) {
    _topology = Topology::None;
    return false;
  }
  _role = _app->server() ? Role::Server : Role::Host;
  _status = Status::Connected;
  _error.clear();
  _token = std::random_device{}();
  _me = _app->server() ? kHostPlayer : 0;
  _hostPlayer = _me;
  _players.clear();
  if (_me >= 0) {
    addPlayer(_me, _name, "", 0);
    _joinedNext.push_back(_me);
  }
  _sentSession = nlohmann::json::object();
  JM_LOG_INFO("[Net] hosting a {} session on UDP port {}", _topology == Topology::P2P ? "p2p" : "client/server",
              _transport->port());
  hostScene();
  if (_me >= 0) spawnPlayerPrefabs({_me});
  return true;
}

bool NetModule::join(const std::string& address) {
  if (online()) {
    _error = "already in a session (Net.leave() first)";
    return false;
  }
  if (!openSocket(0)) return false;
  _hostConn = _transport->connect(address);
  if (!_hostConn) {
    _error = "'" + address + "' isn't an address (host:port)";
    return false;
  }
  _conns[_hostConn] = Conn{_hostConn, kUnknown, true};
  _role = Role::Client;
  _status = Status::Connecting;
  _error.clear();
  JM_LOG_INFO("[Net] joining {}", address);
  return true;
}

void NetModule::leave() {
  if (!online()) return;
  JM_LOG_INFO("[Net] leaving the session");
  endSession(Status::Offline, "");
}

void NetModule::endSession(Status status, const std::string& error) {
  for (const auto& [id, conn] : _conns) _transport->disconnect(id);
  _conns.clear();
  for (const auto& [id, player] : _players) {
    if (id != _me) _leftNext.push_back(id);
  }
  dropCopies();
  for (const auto& [id, player] : _players) {
    if (_inputs) _inputs->dropRemote(id);
  }
  _players.clear();
  _hostConn = 0;
  _role = Role::Offline;
  _topology = Topology::None;
  _status = status;
  _error = error;
  _me = kHostPlayer;
  _hostPlayer = kUnknown;
  _sentInput.reset();
  _punching.clear();
  if (!error.empty()) JM_LOG_WARN("[Net] session ended: {}", error);
}

void NetModule::addPlayer(int32_t id, std::string name, std::string address, ConnId conn) {
  Player& p = _players[id];
  p.id = id;
  p.name = std::move(name);
  p.address = std::move(address);
  if (conn) p.conn = conn;
}

std::vector<int32_t> NetModule::players() const {
  std::vector<int32_t> out;
  for (const auto& [id, p] : _players) out.push_back(id);
  return out;
}

std::optional<std::string> NetModule::playerName(int32_t player) const {
  auto it = _players.find(player);
  return it == _players.end() ? std::nullopt : std::optional(it->second.name);
}

std::optional<std::string> NetModule::playerAddress(int32_t player) const {
  auto it = _players.find(player);
  if (it == _players.end()) return std::nullopt;
  if (it->second.conn) return _transport->address(it->second.conn);
  return it->second.address;
}

float NetModule::ping(int32_t player) const {
  const ConnId conn = player == _hostPlayer && _hostConn ? _hostConn : routeTo(player);
  return conn ? _transport->roundTrip(conn) : 0.0f;
}

void NetModule::punch(const std::string& address) {
  if (!_transport->isOpen() && !openSocket(0)) return;
  _punching[address] = _now + kPunchSeconds;
  _transport->punch(address);
}

// ---- The frame ---------------------------------------------------------------------

void NetModule::tickMainThread(Engine&, float) {
  _now = steadySeconds();
  // What arrives now is what the next frame's scripts see.
  _joined = std::exchange(_joinedNext, {});
  _left = std::exchange(_leftNext, {});
  _inbox = std::exchange(_inboxNext, {});

  std::vector<net::TransportEvent> events;
  _transport->poll(events);
  for (const auto& event : events) handleEvent(event);
  releaseDeferred();

  if (online() && _status == Status::Connected) {
    applyBinds();
    sendChanges();
    sendInput();
  }
  if (_trace) _trace.flush();
  if (!_punching.empty() && _now >= _punchTimer) {
    _punchTimer = _now + 0.25;
    for (auto it = _punching.begin(); it != _punching.end();) {
      if (_now > it->second) {
        it = _punching.erase(it);
        continue;
      }
      _transport->punch(it->first);
      ++it;
    }
  }
}

void NetModule::handleEvent(const net::TransportEvent& event) {
  using Kind = net::TransportEvent::Kind;
  switch (event.kind) {
    case Kind::Connected: {
      auto [it, added] = _conns.try_emplace(event.conn, Conn{event.conn});
      _punching.erase(_transport->address(event.conn));
      onConnected(it->second);
      break;
    }
    case Kind::Disconnected:
      onDisconnected(event.conn);
      break;
    case Kind::Received: {
      auto it = _conns.find(event.conn);
      if (it == _conns.end()) break;
      _stats.receivedBytes += event.data.size();
      ++_stats.receivedMessages;
      trace("in", event.data, event.conn);
      handleMessage(it->second, event.data);
      break;
    }
  }
}

void NetModule::onConnected(Conn& conn) {
  if (conn.toHost) {
    send(conn.id, Writer(Msg::Hello)
                      .u16(net::kProtocolVersion)
                      .str(_app->getManifest().name)
                      .str(_app->getManifest().version)
                      .str(_name));
  } else if (conn.player != kUnknown && _topology == Topology::P2P) {
    // A peer we dialed (p2p): introduce ourselves, then what we simulate.
    send(conn.id, Writer(Msg::PeerHello).i32(_me).u32(_token));
    sendOwnedTo(conn.id);
  }
  // Anyone else dialed us: they introduce themselves (Hello or PeerHello).
}

void NetModule::onDisconnected(ConnId id) {
  auto it = _conns.find(id);
  if (it == _conns.end()) return;
  const Conn conn = it->second;
  _conns.erase(it);
  if (conn.toHost) {
    if (_status == Status::Connecting) {
      endSession(Status::Disconnected, _error.empty() ? "couldn't reach the host" : _error);
    } else if (_topology == Topology::P2P && _status == Status::Connected) {
      hostLeft();
    } else if (online()) {
      endSession(Status::Disconnected, "lost the connection to the host");
    }
    return;
  }
  if (conn.player < 0) return;
  if (auto p = _players.find(conn.player); p != _players.end() && p->second.conn == id) p->second.conn = 0;
  // The host decides who's gone; a peer only lost its direct link (the host
  // says PlayerLeave if they've really left).
  if (isHost()) {
    playerLeft(conn.player);
    broadcast(Writer(Msg::PlayerLeave).i32(conn.player));
  }
}

// P2P: the host is gone. The lowest remaining player id hosts now; every
// peer works that out the same way.
void NetModule::hostLeft() {
  const int32_t old = _hostPlayer;
  _hostConn = 0;
  playerLeft(old);
  if (_players.empty()) {
    endSession(Status::Disconnected, "the host left");
    return;
  }
  _hostPlayer = _players.begin()->first;
  if (_hostPlayer == _me) {
    _role = Role::Host;
    _sentSession = _app->getSession().values();
    JM_LOG_INFO("[Net] the host left; this machine hosts now");
    return;
  }
  _hostConn = _players[_hostPlayer].conn;
  if (!_hostConn) {
    endSession(Status::Disconnected, "the host left, and the new host can't be reached");
    return;
  }
  _conns[_hostConn].toHost = true;
  JM_LOG_INFO("[Net] the host left; player {} hosts now", _hostPlayer);
}

void NetModule::admit(Conn& conn, Reader& hello) {
  const uint16_t protocol = hello.u16();
  const std::string game = hello.str(), version = hello.str(), name = hello.str();
  auto reject = [&](const std::string& reason) {
    send(conn.id, Writer(Msg::Reject).str(reason));
    _transport->disconnect(conn.id);
    JM_LOG_INFO("[Net] turned away {}: {}", _transport->address(conn.id), reason);
  };
  if (!hello.ok() || protocol != net::kProtocolVersion) return reject("a different engine version");
  if (game != _app->getManifest().name || version != _app->getManifest().version) {
    return reject("a different game (" + game + " " + version + ")");
  }
  const size_t playing = std::count_if(_players.begin(), _players.end(), [](const auto& p) { return p.first >= 0; });
  if (playing >= _maxPlayers) return reject("the session is full");

  int32_t id = 0;
  while (_players.contains(id)) ++id;
  conn.player = id;
  const std::string address = _transport->address(conn.id);
  addPlayer(id, name.empty() ? "Player " + std::to_string(id + 1) : name, address, conn.id);

  Writer welcome(Msg::Welcome);
  welcome.i32(id).u8(static_cast<uint8_t>(_topology)).i32(_hostPlayer).u32(_token).u16(static_cast<uint16_t>(_players.size()));
  for (const auto& [pid, p] : _players) welcome.i32(pid).str(p.name).str(pid == _me ? "" : playerAddress(pid).value_or(""));
  nlohmann::json session = nlohmann::json::object();
  for (const auto& [key, value] : _app->getSession().values().items()) {
    if (sharedKey(key)) session[key] = value;
  }
  welcome.str(session.dump());
  send(conn.id, welcome);
  sendSnapshot(conn.id);

  // Everyone else hears; p2p peers will be dialed by the newcomer, and open
  // their routers toward it meanwhile.
  Writer joined(Msg::PlayerJoin);
  joined.i32(id).str(_players[id].name).str(address);
  for (const auto& [cid, c] : _conns) {
    if (cid != conn.id && c.player >= 0) send(cid, joined);
  }
  _joinedNext.push_back(id);
  JM_LOG_INFO("[Net] player {} ({}) joined from {}", id, _players[id].name, address);
  spawnPlayerPrefabs({id});
}

void NetModule::handleMessage(Conn& conn, const std::vector<uint8_t>& data) {
  if (data.empty()) return;
  Reader r(data);
  const auto type = static_cast<Msg>(r.u8());
  const Source from{conn.id, conn.player, conn.toHost};
  switch (type) {
    case Msg::Hello:
      if (isHost() && conn.player == kUnknown) admit(conn, r);
      return;
    case Msg::Welcome: {
      if (!conn.toHost || _status != Status::Connecting) return;
      _me = r.i32();
      _topology = static_cast<Topology>(r.u8());
      _hostPlayer = r.i32();
      _token = r.u32();
      conn.player = _hostPlayer;
      const uint16_t count = r.u16();
      std::vector<std::pair<int32_t, std::string>> dial;
      for (uint16_t i = 0; i < count && r.ok(); ++i) {
        const int32_t id = r.i32();
        std::string name = r.str(), address = r.str();
        addPlayer(id, name, address, id == _hostPlayer ? conn.id : 0);
        _joinedNext.push_back(id);
        if (_topology == Topology::P2P && id != _me && id != _hostPlayer && !address.empty()) dial.emplace_back(id, address);
      }
      const nlohmann::json session = nlohmann::json::parse(r.str(), nullptr, false);
      if (!r.ok()) return endSession(Status::Disconnected, "the host sent something unreadable");
      if (session.is_object()) {
        for (const auto& [key, value] : session.items()) _app->getSession().setJson(key, value);
      }
      _status = Status::Connected;
      JM_LOG_INFO("[Net] joined as player {} ({} players)", _me, _players.size());
      for (const auto& [id, address] : dial) {
        const ConnId peer = _transport->connect(address);
        if (!peer) continue;
        _conns[peer] = Conn{peer, id, false};
        _players[id].conn = peer;
      }
      return;
    }
    case Msg::Reject:
      if (conn.toHost) {
        const std::string reason = r.str();
        endSession(Status::Disconnected, reason.empty() ? "the host said no" : reason);
      }
      return;
    case Msg::PeerHello: {
      const int32_t id = r.i32();
      if (_topology != Topology::P2P || r.u32() != _token || !_players.contains(id)) {
        _transport->disconnect(conn.id);
        return;
      }
      conn.player = id;
      _players[id].conn = conn.id;
      sendOwnedTo(conn.id);
      return;
    }
    case Msg::PlayerJoin: {
      if (!conn.toHost) return;
      const int32_t id = r.i32();
      std::string name = r.str(), address = r.str();
      addPlayer(id, name, address, 0);
      _joinedNext.push_back(id);
      if (_topology == Topology::P2P && !address.empty()) punch(address);  // they'll dial us
      return;
    }
    case Msg::PlayerLeave:
      if (conn.toHost) playerLeft(r.i32());
      return;
    case Msg::Session: {
      if (!conn.toHost) return;
      const std::string key = r.str(), value = r.str();
      if (value.empty()) {
        _app->getSession().remove(key);
      } else if (auto json = nlohmann::json::parse(value, nullptr, false); !json.is_discarded()) {
        _app->getSession().setJson(key, std::move(json));
      }
      return;
    }
    case Msg::Input: {
      if (!isHost() || conn.player < 0 || !_inputs) return;
      r.i32();  // the sender's id, which the connection already says
      InputSnapshot snapshot;
      r.read(snapshot.keys.data(), snapshot.keys.size());
      const uint16_t count = r.u16();
      for (uint16_t i = 0; i < count && r.ok(); ++i) {
        InputSnapshot::Action a;
        a.name = r.str();
        a.down = r.u8() != 0;
        a.value = r.f32();
        snapshot.actions.push_back(std::move(a));
      }
      if (r.ok()) _inputs->remote(conn.player).apply(snapshot);
      return;
    }
    case Msg::Message:
      deliverMessage(from, r, data);
      return;
    default:
      receiveEntityMessage(from, data, net::kReliable);
      return;
  }
}

// ---- Sending ------------------------------------------------------------------------

void NetModule::send(ConnId conn, const std::vector<uint8_t>& data, uint8_t channel) {
  if (!conn) return;
  _stats.sentBytes += data.size();
  ++_stats.sentMessages;
  trace("out", data, conn);
  _transport->send(conn, channel, data);
}

void NetModule::broadcast(const std::vector<uint8_t>& data, uint8_t channel, ConnId except) {
  if (!online()) return;
  if (_role == Role::Client && _topology == Topology::ClientServer) {
    if (_hostConn != except) send(_hostConn, data, channel);
    return;
  }
  for (const auto& [id, conn] : _conns) {
    if (id != except && conn.player != kUnknown) send(id, data, channel);
  }
}

NetModule::ConnId NetModule::routeTo(int32_t player) const {
  if (_role == Role::Client && _topology == Topology::ClientServer) return _hostConn;
  if (player == kHostPlayer || player == _hostPlayer) return _hostConn;
  auto it = _players.find(player);
  return it == _players.end() ? 0 : it->second.conn;
}

void NetModule::sendToPlayer(int32_t player, const std::string& name, const std::string& text, double number) {
  const bool toMe = player == _me || (player == kHostPlayer && isHost());
  if (toMe || !online()) {
    _inboxNext.push_back({_me, name, text, number});
    return;
  }
  Writer w(Msg::Message);
  w.i32(_me).u32(0).i32(player).u32(0).u8(0).str(name).str(text).f64(number);
  if (player == kEveryone) {
    broadcast(w);
  } else {
    send(routeTo(player), w);
  }
}

void NetModule::sendToEntity(EntityId from, EntityId to, const std::string& name, const std::string& text,
                             double number, bool everywhere) {
  NetworkComponent* target = netOf(to);
  auto deliverHere = [&] {
    // From this machine: its player while in a session (who sent it, as on any other machine).
    _app->getScriptManager().queueMessage(to, ScriptMessage{from, name, text, number, online() ? _me : ScriptMessage::kLocal});
  };
  if (!online() || !target || target->netId == 0) {
    deliverHere();
    return;
  }
  const NetworkComponent* sender = netOf(from);
  Writer w(Msg::Message);
  w.i32(_me).u32(sender ? sender->netId : 0).i32(0).u32(target->netId).u8(everywhere ? 1 : 0).str(name).str(text).f64(number);
  if (everywhere) {
    deliverHere();
    broadcast(w);
  } else if (isMine(*target)) {
    deliverHere();
  } else {
    send(routeTo(target->owner), w);
  }
}

void NetModule::deliverMessage(const Source& from, Reader& r, const std::vector<uint8_t>& raw) {
  int32_t sender = r.i32();
  const uint32_t fromNet = r.u32();
  const int32_t toPlayer = r.i32();
  const uint32_t toNet = r.u32();
  const bool everywhere = r.u8() != 0;
  std::string name = r.str(), text = r.str();
  const double number = r.f64();
  if (!r.ok()) return;
  // Only a relaying host may speak for someone else.
  if (!from.host) sender = from.player;

  if (toNet != 0) {
    auto it = _tracked.find(toNet);
    const EntityId to = it == _tracked.end() ? kNoEntityId : it->second.entity;
    auto origin = _tracked.find(fromNet);
    const EntityId fromEntity = origin == _tracked.end() ? kNoEntityId : origin->second.entity;
    const NetworkComponent* target = to == kNoEntityId ? nullptr : netOf(to);
    if (everywhere || (target && isMine(*target))) {
      if (target) _app->getScriptManager().queueMessage(to, ScriptMessage{fromEntity, name, text, number, sender});
      if (everywhere && relays()) broadcast(raw, net::kReliable, from.conn);
    } else if (target && relays()) {
      send(routeTo(target->owner), raw);  // to the client that simulates it
    }
    return;
  }

  const bool forMe = toPlayer == _me || toPlayer == kEveryone || (toPlayer == kHostPlayer && isHost());
  if (forMe) _inboxNext.push_back({sender, std::move(name), std::move(text), number});
  if (relays() && toPlayer != _me && toPlayer != kHostPlayer) {
    if (toPlayer == kEveryone) {
      broadcast(raw, net::kReliable, from.conn);
    } else {
      // Forwarded as the original sender, so the recipient knows who.
      Writer w(Msg::Message);
      w.i32(sender).u32(fromNet).i32(toPlayer).u32(0).u8(0).str(name).str(text).f64(number);
      send(routeTo(toPlayer), w);
    }
  }
}

void NetModule::sendInput() {
  if (_role != Role::Client || _me < 0 || !_inputs || !_hostConn) return;
  InputSnapshot snapshot = _inputs->localSnapshot();
  if (_sentInput && *_sentInput == snapshot) return;
  Writer w(Msg::Input);
  w.i32(_me).bytes(snapshot.keys.data(), snapshot.keys.size()).u16(static_cast<uint16_t>(snapshot.actions.size()));
  for (const auto& a : snapshot.actions) w.str(a.name).u8(a.down ? 1 : 0).f32(a.value);
  send(_hostConn, w);
  _sentInput = std::move(snapshot);
}

// ---- Tools ----------------------------------------------------------------------------

void NetModule::trace(const char* direction, const std::vector<uint8_t>& data, ConnId conn) {
  if (!_trace || data.empty()) return;
  static const char* kNames[] = {"?",     "hello",   "welcome", "reject", "peerHello", "playerJoin", "playerLeave",
                                 "scene", "group",   "bind",    "sync",   "spawn",     "destroy",    "state",
                                 "data",  "tags",    "session", "input",  "message"};
  const size_t type = data[0] < std::size(kNames) ? data[0] : 0;
  auto conns = _conns.find(conn);
  const int32_t player = conns == _conns.end() ? kUnknown : conns->second.player;
  _trace << nlohmann::json{{"t", _now},     {"frame", _app->frameCount()}, {"dir", direction},
                           {"type", kNames[type]}, {"bytes", data.size()},     {"peer", player}}
                .dump()
         << "\n";
}

void NetModule::warnOnce(const std::string& key, const std::string& message) {
  if (_warned.insert(key).second) JM_LOG_WARN("[Net] {}", message);
}

void NetModule::describeState(Engine&, nlohmann::json& state) {
  nlohmann::json players = nlohmann::json::array();
  for (const auto& [id, p] : _players) players.push_back({{"id", id}, {"name", p.name}});
  nlohmann::json entities = nlohmann::json::array();
  std::vector<uint32_t> ids;
  for (const auto& [id, t] : _tracked) ids.push_back(id);
  std::sort(ids.begin(), ids.end());
  for (uint32_t id : ids) {
    const Tracked& t = _tracked.at(id);
    const NetworkComponent* n = netOf(t.entity);
    if (!n) continue;
    entities.push_back({{"netId", id},
                        {"entity", std::to_string(t.entity.index) + ":" + std::to_string(t.entity.generation)},
                        {"owner", n->owner},
                        {"controller", n->controller},
                        {"mine", isMine(*n)}});
  }
  state["net"] = {{"role", roleName(_role)},
                  {"topology", _topology == Topology::P2P ? "p2p" : _topology == Topology::ClientServer ? "server" : "none"},
                  {"status", static_cast<int32_t>(_status)},
                  {"player", _me},
                  {"host", _hostPlayer},
                  {"players", players},
                  {"entities", entities},
                  {"error", _error},
                  {"sent", {{"bytes", _stats.sentBytes}, {"messages", _stats.sentMessages}}},
                  {"received", {{"bytes", _stats.receivedBytes}, {"messages", _stats.receivedMessages}}}};
}
