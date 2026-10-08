#pragma once

#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "../core/app/EngineModule.hpp"
#include "../core/ecs/World.hpp"
#include "../inputs/RemoteInput.hpp"
#include "NetworkComponent.hpp"
#include "Transport.hpp"
#include "Wire.hpp"

class Engine;
class InputsModule;

// Multiplayer sessions. One process hosts (a dedicated server, or a player's
// game) and the others join it; with the "p2p" topology the players also
// connect to each other directly. Entities with a NetworkComponent are
// shared: each is simulated by one process (its owner: the host, or a
// player) and copied everywhere else. See docs/networking.md.
//
// Each frame, after the systems (tickMainThread): receive and apply what
// arrived, then send this process's changes. Copies are moved to their
// interpolated place after physics (NetApplySystem).
class NetModule : public EngineModule {
 public:
  enum class Role : int32_t { Offline = 0, Server = 1, Host = 2, Client = 3 };
  enum class Topology : int32_t { None = 0, ClientServer = 1, P2P = 2 };
  enum class Status : int32_t { Offline = 0, Connecting = 1, Connected = 2, Disconnected = 3 };

  static constexpr int32_t kHostPlayer = -1;  // a message target or sender: the host
  static constexpr int32_t kEveryone = -2;    // a message target: every other player

  NetModule();
  ~NetModule() override;

  void registerComponents(Engine& app) override;
  void bindScriptApi(Engine& app) override;  // NetScriptApi.cpp
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  void tickMainThread(Engine& app, float dt) override;
  // state["net"]: role, players, and every shared entity's id and owner.
  void describeState(Engine& app, nlohmann::json& state) override;
  const char* name() const override { return "NetModule"; }

  // Sessions. host: listen on `port` (0: .jm.json's net.port; with p2p, the
  // socket this machine already has open, so a matchmaker's view of its
  // address stays valid). join: connect to "host:port". False (with
  // lastError()) if it can't start.
  bool host(uint16_t port, Topology topology = Topology::None);
  bool join(const std::string& address);
  void leave();

  Role role() const { return _role; }
  Topology topology() const { return _topology; }
  Status status() const { return _status; }
  bool online() const { return _role != Role::Offline; }
  bool isHost() const { return _role == Role::Server || _role == Role::Host; }
  // This machine's player id; kHostPlayer on a dedicated server (and offline).
  int32_t localPlayer() const { return _me; }
  int32_t hostPlayer() const { return _hostPlayer; }
  std::vector<int32_t> players() const;
  const std::vector<int32_t>& joinedThisFrame() const { return _joined; }
  const std::vector<int32_t>& leftThisFrame() const { return _left; }
  std::optional<std::string> playerName(int32_t player) const;
  std::optional<std::string> playerAddress(int32_t player) const;
  float ping(int32_t player) const;  // round trip, seconds
  const std::string& lastError() const { return _error; }
  void setLocalName(std::string name) { _name = std::move(name); }
  uint16_t localPort() const { return _transport ? _transport->port() : 0; }
  // Keeps a hole open toward `address` for a few seconds (p2p through NAT).
  void punch(const std::string& address);

  // Whether this process simulates `entity` (always, offline or for entities
  // without a NetworkComponent).
  bool simulatesHere(EntityId entity) const;
  bool isMine(const NetworkComponent& net) const;

  // Host: spawns `prefab` for `player`, who controls it (and simulates it,
  // when its Network says authority "owner").
  EntityId spawnFor(int32_t player, const std::string& prefab, float x, float y, nlohmann::json overrides);

  // A message to a shared entity's script: where it's simulated, or (everywhere)
  // to every copy. Delivered as onMessage, with Message.player() the sender.
  void sendToEntity(EntityId from, EntityId to, const std::string& name, const std::string& text, double number,
                    bool everywhere);
  // A message to a player (kHostPlayer: the host, kEveryone: all the others),
  // read from Net.messages() in the frame after it arrives.
  void sendToPlayer(int32_t player, const std::string& name, const std::string& text, double number);
  struct Inbound {
    int32_t from;
    std::string name, text;
    double number;
  };
  const std::vector<Inbound>& inbox() const { return _inbox; }

  // NetApplySystem: every copy to where it should be now.
  void applyCopies(World& world);

 private:
  using ConnId = net::ConnId;
  using Msg = net::Msg;
  using Writer = net::Writer;
  using Reader = net::Reader;
  static constexpr int32_t kUnknown = -100;

  struct Player {
    int32_t id = 0;
    std::string name;
    std::string address;  // as the host sees it
    ConnId conn = 0;      // this process's connection to them, if any
  };
  struct Conn {
    ConnId id = 0;
    int32_t player = kUnknown;
    bool toHost = false;  // our connection to the session's host
  };
  // Who a message came from, for checking what they may change.
  struct Source {
    ConnId conn = 0;
    int32_t player = kUnknown;
    bool host = false;
  };
  // Replicated script fields by (place in the replicate list << 8 | field index), raw bits.
  using Values = std::map<uint16_t, uint32_t>;
  struct Sample {
    double time;
    Values values;
  };
  // A shared entity this process knows.
  struct Tracked {
    EntityId entity;
    bool announced = false;  // simulated here: the others know it (its Spawn or Bind went out)
    // Sending (where it's simulated). A change goes out in a few sends in a
    // row, and every field now and then, so a lost update heals.
    Values sent;
    std::map<uint16_t, int32_t> repeats;
    int32_t refreshIn = 0;
    nlohmann::json sentData = nlohmann::json::object();
    std::vector<std::string> sentTags;
    // Receiving (copies): newest values, and timed samples to interpolate.
    int32_t origin = kUnknown;
    Values latest;
    std::deque<Sample> samples;
  };

  Engine* _app = nullptr;
  InputsModule* _inputs = nullptr;
  std::unique_ptr<net::Transport> _transport;
  nlohmann::json _config = nlohmann::json::object();
  size_t _maxPlayers = 8;
  // The host's scene is everyone's (net.shareScene, default true): joiners
  // load it and follow its changes. Off, each machine keeps its own scenes.
  bool _shareScene = true;

  Role _role = Role::Offline;
  Topology _topology = Topology::None;
  Status _status = Status::Offline;
  int32_t _me = kHostPlayer;
  int32_t _hostPlayer = kUnknown;
  uint32_t _token = 0;
  std::string _name = "Player";
  std::string _error;
  std::map<int32_t, Player> _players;
  std::unordered_map<ConnId, Conn> _conns;
  ConnId _hostConn = 0;
  std::vector<int32_t> _joined, _left, _joinedNext, _leftNext;
  std::vector<Inbound> _inbox, _inboxNext;
  std::map<std::string, double> _punching;  // address -> until when
  double _punchTimer = 0.0;

  // Shared entities.
  uint32_t _nextNetId = 1;
  std::unordered_map<uint32_t, Tracked> _tracked;
  // Scenes: the host numbers each load (epoch); copies pair up by entry key.
  uint32_t _epoch = 0;
  std::map<std::string, EntityId> _keyed;           // this scene's entries with a Network, by key
  std::map<std::string, uint32_t> _binds;           // client: entry key -> net id, from the host
  int32_t _sceneOpsPending = 0;                     // client: scene loads and groups the host asked for, not done
  struct Deferred {
    Source from;
    std::vector<uint8_t> data;
    uint8_t channel;
  };
  std::deque<Deferred> _deferred;  // entity messages waiting for those
  bool _unloading = false;
  std::unordered_map<EntityId, int32_t> _spawningFor;  // host: spawned for a player, not built yet
  // How each shared entity spawned at run time was made: what a late joiner is sent.
  struct SpawnInfo {
    std::string prefab;
    float x, y;
    nlohmann::json overrides;
  };
  std::unordered_map<EntityId, SpawnInfo> _spawnInfo;
  std::unordered_set<EntityId> _cancelled;  // spawned from a Spawn, destroyed before it was built
  struct Incoming {
    uint32_t netId;
    int32_t owner, controller;
  };
  std::unordered_map<EntityId, Incoming> _incoming;  // spawned from a peer's Spawn, not built yet
  nlohmann::json _sentSession = nlohmann::json::object();
  std::set<std::string> _warned;

  // Timing.
  double _now = 0.0;  // steady seconds
  double _nextSend = 0.0;
  double _sendInterval = 1.0 / 30.0;
  double _interpolationDelay = 0.1;
  std::map<int32_t, double> _clockOffset;  // origin player -> local time minus their time (least seen)
  std::optional<InputSnapshot> _sentInput;
  std::ofstream _trace;
  struct Stats {
    uint64_t sentBytes = 0, receivedBytes = 0, sentMessages = 0, receivedMessages = 0;
  } _stats;

  // NetModule.cpp: sessions and routing.
  bool openSocket(uint16_t port);
  void startServerScripts();
  void handleEvent(const net::TransportEvent& event);
  void handleMessage(Conn& conn, const std::vector<uint8_t>& data);
  void onConnected(Conn& conn);
  void onDisconnected(ConnId id);
  void admit(Conn& conn, Reader& hello);
  void addPlayer(int32_t id, std::string name, std::string address, ConnId conn);
  void hostLeft();
  void endSession(Status status, const std::string& error);
  void send(ConnId conn, const std::vector<uint8_t>& data, uint8_t channel = net::kReliable);
  void send(ConnId conn, const Writer& message, uint8_t channel = net::kReliable) { send(conn, message.data(), channel); }
  // To everyone who should hear this process's news: the host (from a
  // client of a server), or every connected player. `except`: a relayed
  // message's source.
  void broadcast(const std::vector<uint8_t>& data, uint8_t channel = net::kReliable, ConnId except = 0);
  void broadcast(const Writer& message, uint8_t channel = net::kReliable) { broadcast(message.data(), channel); }
  // The connection a message for `player` goes out on (a client of a server: the server's).
  ConnId routeTo(int32_t player) const;
  bool relays() const { return isHost() && _topology == Topology::ClientServer; }
  void deliverMessage(const Source& from, Reader& r, const std::vector<uint8_t>& raw);
  void trace(const char* direction, const std::vector<uint8_t>& data, ConnId conn);
  void warnOnce(const std::string& key, const std::string& message);

  // NetReplication.cpp: shared entities.
  void hookEngine();
  void onSpawned(EntityId id, const std::string& prefab, float x, float y, const nlohmann::json& overrides,
                 EntityId by);
  void onComponentDestroyed(NetworkComponent& net);
  NetworkComponent* netOf(EntityId id) const;
  Tracked* trackedOf(EntityId id);
  uint32_t allocateNetId();
  Tracked& track(uint32_t netId, EntityId entity, NetworkComponent& net);
  // Destroys this copy (no Destroy goes out): it's gone everywhere already.
  void forget(uint32_t netId);
  void hostScene();  // host: number the current scene and pair its entries
  void hostGroup(const std::string& group, bool spawned);
  // Host: this scene's entries under `prefix` and their net ids (made for
  // those that lack one), as a Bind.
  Writer bindMessage(const std::string& prefix);
  void applyBinds();  // client: pair up the entries the host named
  void sendSnapshot(ConnId conn);  // host: everything to a joiner
  void sendOwnedTo(ConnId conn);   // p2p: what this process simulates, to a newly linked peer
  Writer spawnMessage(uint32_t netId, const Tracked& t, const NetworkComponent& net, const SpawnInfo& info);
  // Every replicated field, as one entity of a State.
  Writer fullState(uint32_t netId, const Tracked& t, const NetworkComponent& net);
  Values valuesOf(const Tracked& t, const NetworkComponent& net) const;
  static void writeValues(Writer& w, const Values& values);
  static Values readValues(Reader& r);
  void sendExtras(ConnId conn, uint32_t netId, const Tracked& t);  // its data and tags, to one connection
  std::optional<World::ScriptFieldRef> fieldOf(const NetworkComponent& net, uint16_t key) const;
  void receiveSamples(Tracked& t, const Values& delta, double time, int32_t origin);
  void sendChanges();
  void sendSession();  // host: the session store's changes
  // Session keys the host mirrors: all but "local.*", which stay on their machine.
  static bool sharedKey(const std::string& key);
  void sendInput();
  void receiveEntityMessage(const Source& from, const std::vector<uint8_t>& data, uint8_t channel);
  void handleEntityMessage(const Source& from, const std::vector<uint8_t>& data, uint8_t channel);
  void releaseDeferred();
  void playerLeft(int32_t player);
  void dropCopies();  // the session's over: copies of what others simulated go
  void spawnPlayerPrefabs(const std::vector<int32_t>& players);
};
