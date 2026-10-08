// Shared entities: which process simulates each, pairing scene entries up,
// spawning and destroying copies, streaming fields, data and tags, and
// moving copies along between updates.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/Engine.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "../core/logger/logging.hpp"
#include "../inputs/InputsModule.hpp"
#include "NetModule.hpp"

using net::Msg;
using net::Reader;
using net::Writer;

namespace {

// Copies to their place between the updates they're sent, after physics
// moved them (their velocity) and before anything draws them.
class NetApplySystem : public System {
 public:
  explicit NetApplySystem(NetModule& net) : _net(net) {}
  void update(World& world, float) override { _net.applyCopies(world); }
  const char* name() const override { return "NetApplySystem"; }

 private:
  NetModule& _net;
};

constexpr size_t kMaxStateBytes = 1000;  // one State per UDP packet
constexpr int32_t kRepeats = 2;           // sends after a change that still carry it
constexpr double kRefreshSeconds = 1.0;   // every field, this often

float lerpAngle(float a, float b, float t) {
  float d = std::fmod(b - a, 6.2831853f);
  if (d > 3.14159265f) d -= 6.2831853f;
  if (d < -3.14159265f) d += 6.2831853f;
  return a + d * t;
}

}  // namespace

template <>
struct SystemTraits<NetApplySystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};

void NetModule::registerComponents(Engine& app) {
  app.getWorld().registerComponent<NetworkComponent>({
      .fromJson = [this, &app](NetworkComponent& c, const nlohmann::json& json, EntityId id) {
        c.ownerSimulates = json.value("authority", std::string("host")) == "owner";
        if (auto it = json.find("replicate"); it != json.end() && it->is_array()) {
          c.replicate.clear();
          for (const auto& name : *it) {
            if (name.is_string()) c.replicate.push_back(name.get<std::string>());
          }
        }
        c.interpolate = json.value("interpolate", true);
        c.scriptsEverywhere = json.value("scripts", std::string("authority")) == "everywhere";
        c.keepWhenOwnerLeaves = json.value("ownerLeaves", std::string("destroy")) == "host";
        c.entity = id;
        c.sceneKey = app.getSceneManager().loader().entryKey();
        if (!c.sceneKey.empty()) {
          c.epoch = _epoch;
          _keyed[c.sceneKey] = id;
        }
      },
      .onDestroy = [this](NetworkComponent& c) { onComponentDestroyed(c); },
      .schema = {"Network", "Multiplayer", "Shared with everyone in a multiplayer session",
                 {FieldSchema::choice("authority", {"host", "owner"},
                                      "Who simulates it: the host, or the player it's spawned for"),
                  FieldSchema::json("replicate", "Components kept in sync (default [\"TransformComponent\"])"),
                  FieldSchema::boolean("interpolate", true, "Copies move smoothly between updates"),
                  FieldSchema::choice("scripts", {"authority", "everywhere"},
                                      "Run its script only where it's simulated, or on every copy too"),
                  FieldSchema::choice("ownerLeaves", {"destroy", "host"},
                                      "When its player leaves: it goes, or the host takes it over")}},
  });
}

void NetModule::hookEngine() {
  Engine& app = *_app;
  app.getWorld().registerSystem<NetApplySystem>(*this);
  app.getSpawner().addListener([this](const EntitySpawner::Spawned& s) {
    onSpawned(s.id, s.prefabPath, s.x, s.y, s.overrides, s.by);
  });

  SceneManager& scenes = app.getSceneManager();
  scenes.addUnloadListener([this] {
    _unloading = true;  // the scene's entities go everywhere: no Destroy for each
    _keyed.clear();
  });
  auto sceneDone = [this] {
    _unloading = false;
    if (isHost()) {
      hostScene();
      spawnPlayerPrefabs(players());
    } else if (_sceneOpsPending > 0) {
      --_sceneOpsPending;
    }
  };
  app.getEventBus().subscribe<events::SceneLoaded>(EVT_SceneLoaded, [sceneDone](const events::SceneLoaded&) { sceneDone(); });
  app.getEventBus().subscribe<events::SceneLoadFailed>(EVT_SceneLoadFailed,
                                                       [sceneDone](const events::SceneLoadFailed&) { sceneDone(); });
  scenes.addGroupListener([this](const std::string& group, bool spawned) {
    if (isHost()) {
      hostGroup(group, spawned);
    } else if (_sceneOpsPending > 0) {
      --_sceneOpsPending;
    }
  });

  // Scripts run where their entity is simulated; input comes from whoever controls it.
  app.getScriptManager().setRunFilter([this](EntityId id) {
    const NetworkComponent* n = netOf(id);
    return !n || n->scriptsEverywhere || isMine(*n);
  });
  if (_inputs) {
    _inputs->setControllerResolver([this](EntityId id) -> int32_t {
      const NetworkComponent* n = online() ? netOf(id) : nullptr;
      return n && n->controller >= 0 && n->controller != _me ? n->controller : -1;
    });
  }
}

NetworkComponent* NetModule::netOf(EntityId id) const {
  return _app->getWorld().isAlive(id) ? _app->getWorld().getComponent<NetworkComponent>(id) : nullptr;
}

NetModule::Tracked* NetModule::trackedOf(EntityId id) {
  const NetworkComponent* n = netOf(id);
  auto it = n ? _tracked.find(n->netId) : _tracked.end();
  return it == _tracked.end() ? nullptr : &it->second;
}

bool NetModule::isMine(const NetworkComponent& net) const {
  if (net.gone) return false;
  if (!online()) return true;
  if (net.netId == 0) return net.sceneKey.empty() || isHost();  // unpaired scene entries are the host's
  if (net.owner == NetworkComponent::kHost) return isHost();
  return net.owner == _me;
}

bool NetModule::simulatesHere(EntityId entity) const {
  const NetworkComponent* n = netOf(entity);
  return !n || isMine(*n);
}

uint32_t NetModule::allocateNetId() {
  // The top byte says whose: ids made by different processes never collide.
  return (static_cast<uint32_t>(_me + 2) << 24) | (_nextNetId++ & 0xFFFFFFu);
}

NetModule::Tracked& NetModule::track(uint32_t netId, EntityId entity, NetworkComponent& net) {
  net.netId = netId;
  Tracked& t = _tracked[netId];
  t.entity = entity;
  return t;
}

void NetModule::forget(uint32_t netId) {
  auto it = _tracked.find(netId);
  if (it == _tracked.end()) return;
  const EntityId entity = it->second.entity;
  _tracked.erase(it);
  if (_incoming.erase(entity)) _cancelled.insert(entity);
  if (NetworkComponent* n = netOf(entity)) {
    n->gone = true;
    n->netId = 0;
  }
  if (_app->getWorld().isAlive(entity)) _app->getWorld().destroyDeferred(entity);
}

// ---- Spawning and destroying ---------------------------------------------------------

EntityId NetModule::spawnFor(int32_t player, const std::string& prefab, float x, float y, nlohmann::json overrides) {
  if (online() && !isHost()) {
    warnOnce("spawnFor", "Net.spawnPlayer is the host's to call; ignored on player " + std::to_string(_me));
    return kNoEntityId;
  }
  const EntityId id =
      _app->getSpawner().spawn(_app->getManifest().resolve(prefab, ".prefab.json"), x, y, std::move(overrides));
  _spawningFor[id] = player;
  return id;
}

void NetModule::onSpawned(EntityId id, const std::string& prefab, float x, float y, const nlohmann::json& overrides,
                          EntityId by) {
  NetworkComponent* n = netOf(id);
  auto incoming = _incoming.find(id);
  auto spawningFor = _spawningFor.find(id);
  const std::optional<Incoming> remote =
      incoming == _incoming.end() ? std::nullopt : std::optional<Incoming>(incoming->second);
  const std::optional<int32_t> forPlayer =
      spawningFor == _spawningFor.end() ? std::nullopt : std::optional<int32_t>(spawningFor->second);
  if (remote) _incoming.erase(incoming);
  if (forPlayer) _spawningFor.erase(spawningFor);
  if (!n) return;
  if (_cancelled.erase(id)) {
    n->gone = true;
    return;
  }
  _spawnInfo[id] = SpawnInfo{prefab, x, y, overrides};

  if (remote) {  // a copy of someone else's
    n->owner = remote->owner;
    n->controller = remote->controller;
    n->netId = remote->netId;
    if (auto t = _tracked.find(remote->netId); t != _tracked.end()) t->second.entity = id;
    return;
  }

  if (forPlayer) {
    n->owner = n->ownerSimulates ? *forPlayer : NetworkComponent::kHost;
    n->controller = *forPlayer;
  } else if (const NetworkComponent* parent = by == kNoEntityId ? nullptr : netOf(by)) {
    if (!isMine(*parent)) {
      // A copy's script (scripts: "everywhere") spawned it: a local effect.
      warnOnce("copy-spawn:" + prefab, "a copy's script spawned shared '" + prefab +
                                           "'; it stays on this machine (spawn it where the spawner is simulated)");
      return;
    }
    n->owner = parent->owner;
    n->controller = parent->controller;
  } else {
    n->owner = !online() || isHost() ? NetworkComponent::kHost : _me;
    if (online() && !isHost()) {
      warnOnce("unshared-spawner:" + prefab,
               "a script that runs on every machine spawned shared '" + prefab +
                   "' on player " + std::to_string(_me) +
                   ": every player makes one. Give the spawner a Network component so only the host runs it.");
    }
  }
  track(allocateNetId(), id, *n);  // announced by the next sendChanges
}

void NetModule::onComponentDestroyed(NetworkComponent& net) {
  _spawnInfo.erase(net.entity);
  if (net.netId == 0) return;
  const uint32_t id = net.netId;
  // The host may remove anything; others only what they simulate.
  const bool tell = online() && !_unloading && (isMine(net) || isHost());
  _tracked.erase(id);
  net.netId = 0;
  if (tell) broadcast(Writer(Msg::Destroy).u32(id));
}

void NetModule::spawnPlayerPrefabs(const std::vector<int32_t>& list) {
  const std::string prefab = _config.value("playerPrefab", std::string());
  if (prefab.empty() || !isHost()) return;
  World& world = _app->getWorld();
  std::vector<EntityId> points;
  for (EntityId e : world.findWithTag("spawn")) {
    if (world.isAlive(e) && !world.isPendingDestroy(e)) points.push_back(e);
  }
  if (points.empty()) return;  // a scene without spawn points (a menu) has no avatars
  std::sort(points.begin(), points.end(), [](EntityId a, EntityId b) { return a.index < b.index; });
  const auto fx = world.findScriptField("TransformComponent", "x");
  const auto fy = world.findScriptField("TransformComponent", "y");
  for (int32_t player : list) {
    if (player < 0) continue;
    const EntityId at = points[static_cast<size_t>(player) % points.size()];
    float x = 0, y = 0;
    if (fx && fy) {
      const uint32_t bx = world.readScriptField(at, *fx).value_or(0), by = world.readScriptField(at, *fy).value_or(0);
      std::memcpy(&x, &bx, 4);
      std::memcpy(&y, &by, 4);
    }
    spawnFor(player, prefab, x, y, nlohmann::json::object());
  }
}

void NetModule::playerLeft(int32_t player) {
  if (!_players.erase(player)) return;
  _leftNext.push_back(player);
  if (_inputs) _inputs->dropRemote(player);
  _clockOffset.erase(player);
  std::vector<uint32_t> ids;
  for (const auto& [id, t] : _tracked) ids.push_back(id);
  for (uint32_t id : ids) {
    auto it = _tracked.find(id);
    if (it == _tracked.end()) continue;
    NetworkComponent* n = netOf(it->second.entity);
    if (!n || (n->owner != player && n->controller != player)) continue;
    // Every process does this itself: the player can't say goodbye for what they had.
    if (n->keepWhenOwnerLeaves) {
      if (n->owner == player) n->owner = NetworkComponent::kHost;
      n->controller = NetworkComponent::kNobody;
      it->second.sent.clear();
      it->second.announced = true;
    } else {
      forget(id);
    }
  }
  JM_LOG_INFO("[Net] player {} left", player);
}

void NetModule::dropCopies() {
  for (auto& [id, t] : _tracked) {
    NetworkComponent* n = netOf(t.entity);
    if (!n) continue;
    if (isMine(*n)) {
      n->netId = 0;
      n->owner = NetworkComponent::kHost;
      n->controller = NetworkComponent::kNobody;
    } else {
      n->gone = true;
      n->netId = 0;
      _app->getWorld().destroyDeferred(t.entity);
    }
  }
  _tracked.clear();
  _incoming.clear();
  _binds.clear();
  _deferred.clear();
  _sceneOpsPending = 0;
  _clockOffset.clear();
}

// ---- Scenes ---------------------------------------------------------------------------

void NetModule::hostScene() {
  if (!online() || !isHost() || !_shareScene) return;
  ++_epoch;
  SceneManager& scenes = _app->getSceneManager();
  broadcast(Writer(Msg::Scene).u32(_epoch).str(scenes.getCurrentScenePath()));
  for (const std::string& group : scenes.spawnedGroups()) broadcast(Writer(Msg::Group).u32(_epoch).str(group).u8(1));
  for (auto& [key, entity] : _keyed) {
    if (NetworkComponent* n = netOf(entity)) n->epoch = _epoch;
  }
  broadcast(bindMessage(""));
  broadcast(Writer(Msg::Sync).u32(_epoch).str(""));
  // Shared entities spawned before hosting (offline) are shared from now on.
  for (auto [entity, n] : _app->getWorld().view<NetworkComponent>()) {
    if (n->netId == 0 && n->sceneKey.empty() && !n->gone && _spawnInfo.contains(entity)) {
      track(allocateNetId(), entity, *n);
    }
  }
}

void NetModule::hostGroup(const std::string& group, bool spawned) {
  if (!online() || !_shareScene) return;
  broadcast(Writer(Msg::Group).u32(_epoch).str(group).u8(spawned ? 1 : 0));
  if (!spawned) return;
  const std::string prefix = "g:" + group + "/";
  broadcast(bindMessage(prefix));
  broadcast(Writer(Msg::Sync).u32(_epoch).str(prefix));
}

Writer NetModule::bindMessage(const std::string& prefix) {
  Writer w(Msg::Bind);
  w.u32(_epoch);
  const size_t countAt = w.mark();
  w.u16(0);
  uint16_t count = 0;
  for (auto& [key, entity] : _keyed) {
    if (!key.starts_with(prefix)) continue;
    NetworkComponent* n = netOf(entity);
    if (!n || n->gone || _app->getWorld().isPendingDestroy(entity)) continue;
    if (n->netId == 0) {
      Tracked& t = track(allocateNetId(), entity, *n);
      t.announced = true;
      t.sentTags = _app->getWorld().tagNames(entity);  // the scene gives every copy these
    }
    w.str(key).u32(n->netId);
    ++count;
  }
  w.patchU16(countAt, count);
  return w;
}

void NetModule::applyBinds() {
  if (isHost()) return;
  for (const auto& [key, netId] : _binds) {
    auto keyed = _keyed.find(key);
    if (keyed == _keyed.end()) continue;
    NetworkComponent* n = netOf(keyed->second);
    if (!n || n->gone || n->netId != 0) continue;
    if (_tracked.contains(netId)) continue;
    n->owner = NetworkComponent::kHost;
    track(netId, keyed->second, *n);
  }
}

// ---- What's sent ----------------------------------------------------------------------

NetModule::Values NetModule::valuesOf(const Tracked& t, const NetworkComponent& net) const {
  if (!isMine(net)) return t.latest;  // a copy: what its owner said (this build may lack some components)
  Values values;
  const World& world = _app->getWorld();
  for (size_t c = 0; c < net.replicate.size() && c < 256; ++c) {
    const ComponentInfo* info = world.getComponentRegistry().getInfoByName(net.replicate[c]);
    if (!info) continue;
    for (uint32_t f = 0; f < info->scriptFields.size() && f < 256; ++f) {
      if (auto bits = world.readScriptField(t.entity, {info, f})) values[static_cast<uint16_t>(c << 8 | f)] = *bits;
    }
  }
  return values;
}

std::optional<World::ScriptFieldRef> NetModule::fieldOf(const NetworkComponent& net, uint16_t key) const {
  const size_t c = key >> 8;
  const uint32_t f = key & 0xFF;
  if (c >= net.replicate.size()) return std::nullopt;
  const ComponentInfo* info = _app->getWorld().getComponentRegistry().getInfoByName(net.replicate[c]);
  if (!info || f >= info->scriptFields.size()) return std::nullopt;
  return World::ScriptFieldRef{info, f};
}

void NetModule::writeValues(Writer& w, const Values& values) {
  w.u16(static_cast<uint16_t>(values.size()));
  for (const auto& [key, bits] : values) w.u16(key).u32(bits);
}

NetModule::Values NetModule::readValues(Reader& r) {
  Values values;
  const uint16_t count = r.u16();
  for (uint16_t i = 0; i < count && r.ok(); ++i) {
    const uint16_t key = r.u16();
    values[key] = r.u32();
  }
  return values;
}

Writer NetModule::spawnMessage(uint32_t netId, const Tracked& t, const NetworkComponent& net, const SpawnInfo& info) {
  Writer w(Msg::Spawn);
  w.u32(netId).i32(net.owner).i32(net.controller).str(info.prefab).f32(info.x).f32(info.y).str(info.overrides.dump());
  writeValues(w, valuesOf(t, net));
  return w;
}

Writer NetModule::fullState(uint32_t netId, const Tracked& t, const NetworkComponent& net) {
  Writer w(Msg::State);
  w.i32(isMine(net) ? _me : t.origin).f64(_now).u16(1).u32(netId);
  writeValues(w, valuesOf(t, net));
  return w;
}

void NetModule::sendExtras(ConnId conn, uint32_t netId, const Tracked& t) {
  if (GameState* store = _app->getEntityStores().storeOf(t.entity)) {
    for (const auto& [key, value] : store->values().items()) send(conn, Writer(Msg::Data).u32(netId).str(key).str(value.dump()));
  }
  const auto tags = _app->getWorld().tagNames(t.entity);
  Writer w(Msg::Tags);
  w.u32(netId).u16(static_cast<uint16_t>(tags.size()));
  for (const auto& tag : tags) w.str(tag);
  send(conn, w);
}

void NetModule::sendSnapshot(ConnId conn) {
  if (_shareScene) {
    SceneManager& scenes = _app->getSceneManager();
    send(conn, Writer(Msg::Scene).u32(_epoch).str(scenes.getCurrentScenePath()));
    for (const std::string& group : scenes.spawnedGroups()) send(conn, Writer(Msg::Group).u32(_epoch).str(group).u8(1));
    send(conn, bindMessage(""));
    send(conn, Writer(Msg::Sync).u32(_epoch).str(""));
  }
  std::vector<uint32_t> ids;
  for (const auto& [id, t] : _tracked) ids.push_back(id);
  std::sort(ids.begin(), ids.end());
  for (uint32_t id : ids) {
    const Tracked& t = _tracked.at(id);
    const NetworkComponent* n = netOf(t.entity);
    if (!n || n->gone) continue;
    if (n->sceneKey.empty()) {
      auto info = _spawnInfo.find(t.entity);
      if (info == _spawnInfo.end()) continue;
      send(conn, spawnMessage(id, t, *n, info->second));
    } else {
      send(conn, fullState(id, t, *n));  // reliable: the joiner starts from it
    }
    sendExtras(conn, id, t);
  }
}

void NetModule::sendOwnedTo(ConnId conn) {
  for (const auto& [id, t] : _tracked) {
    const NetworkComponent* n = netOf(t.entity);
    if (!n || !isMine(*n) || !n->sceneKey.empty()) continue;
    auto info = _spawnInfo.find(t.entity);
    if (info == _spawnInfo.end()) continue;
    send(conn, spawnMessage(id, t, *n, info->second));
    sendExtras(conn, id, t);
  }
}

bool NetModule::sharedKey(const std::string& key) { return !key.starts_with("local."); }

void NetModule::sendSession() {
  const nlohmann::json& now = _app->getSession().values();
  for (const auto& [key, value] : now.items()) {
    if (!sharedKey(key)) continue;
    auto sent = _sentSession.find(key);
    if (sent == _sentSession.end() || *sent != value) broadcast(Writer(Msg::Session).str(key).str(value.dump()));
  }
  for (const auto& [key, value] : _sentSession.items()) {
    if (!now.contains(key) && sharedKey(key)) broadcast(Writer(Msg::Session).str(key).str(""));
  }
  _sentSession = now;
}

void NetModule::sendChanges() {
  World& world = _app->getWorld();
  for (auto it = _tracked.begin(); it != _tracked.end();) {
    // Gone without a word (failed to build): nothing to send for it.
    if (!world.isAlive(it->second.entity) && !_incoming.contains(it->second.entity)) {
      it = _tracked.erase(it);
    } else {
      ++it;
    }
  }
  // New shared entities first, so their state has somewhere to go: those
  // spawned here, including the host's spawns for players who simulate them.
  for (auto& [id, t] : _tracked) {
    const NetworkComponent* n = netOf(t.entity);
    if (!n || n->gone || t.announced) continue;
    t.announced = true;
    auto info = _spawnInfo.find(t.entity);
    if (!n->sceneKey.empty() || info == _spawnInfo.end()) continue;
    broadcast(spawnMessage(id, t, *n, info->second));
    t.sent = valuesOf(t, *n);
    t.sentTags = _app->getWorld().tagNames(t.entity);  // the prefab (and its overrides) gives every copy these
    t.refreshIn = static_cast<int32_t>(kRefreshSeconds / _sendInterval);
  }

  if (_now < _nextSend) return;
  _nextSend = std::max(_nextSend + _sendInterval, _now);
  if (isHost()) sendSession();

  Writer state(Msg::State);
  size_t countAt = 0;
  uint16_t count = 0;
  auto startState = [&] {
    state = Writer(Msg::State);
    state.i32(_me).f64(_now);
    countAt = state.mark();
    state.u16(0);
    count = 0;
  };
  auto flushState = [&] {
    if (count == 0) return;
    state.patchU16(countAt, count);
    broadcast(state.data(), net::kUnreliable);
  };
  startState();

  const int32_t refreshEvery = std::max(1, static_cast<int32_t>(kRefreshSeconds / _sendInterval));
  for (auto& [id, t] : _tracked) {
    const NetworkComponent* n = netOf(t.entity);
    if (!n || n->gone || !t.announced || !isMine(*n)) continue;

    // Fields: changes (repeated a few sends), everything now and then.
    const Values now = valuesOf(t, *n);
    Values delta;
    const bool refresh = --t.refreshIn <= 0;
    if (refresh) t.refreshIn = refreshEvery + static_cast<int32_t>(id % 7);
    for (const auto& [key, bits] : now) {
      auto sent = t.sent.find(key);
      if (sent == t.sent.end() || sent->second != bits) t.repeats[key] = kRepeats + 1;
      auto& repeats = t.repeats[key];
      if (refresh || repeats > 0) delta[key] = bits;
      if (repeats > 0) --repeats;
    }
    t.sent = now;
    if (!delta.empty()) {
      state.u32(id);
      writeValues(state, delta);
      ++count;
      if (state.size() > kMaxStateBytes) {
        flushState();
        startState();
      }
    }

    // entity.data and tags: what changed, in order.
    GameState* store = _app->getEntityStores().storeOf(t.entity);
    const nlohmann::json data = store ? store->values() : nlohmann::json::object();
    if (data != t.sentData) {
      for (const auto& [key, value] : data.items()) {
        auto sent = t.sentData.find(key);
        if (sent == t.sentData.end() || *sent != value) broadcast(Writer(Msg::Data).u32(id).str(key).str(value.dump()));
      }
      for (const auto& [key, value] : t.sentData.items()) {
        if (!data.contains(key)) broadcast(Writer(Msg::Data).u32(id).str(key).str(""));
      }
      t.sentData = data;
    }
    auto tags = world.tagNames(t.entity);
    if (tags != t.sentTags) {
      Writer w(Msg::Tags);
      w.u32(id).u16(static_cast<uint16_t>(tags.size()));
      for (const auto& tag : tags) w.str(tag);
      broadcast(w);
      t.sentTags = std::move(tags);
    }
  }
  flushState();
}

// ---- What's received ------------------------------------------------------------------

void NetModule::receiveEntityMessage(const Source& from, const std::vector<uint8_t>& data, uint8_t channel) {
  // While a scene or group the host asked for is still loading, what follows
  // waits: it's about entities that don't exist yet.
  if (_sceneOpsPending > 0) {
    _deferred.push_back({from, data, channel});
    return;
  }
  handleEntityMessage(from, data, channel);
}

void NetModule::releaseDeferred() {
  while (_sceneOpsPending == 0 && !_deferred.empty()) {
    Deferred next = std::move(_deferred.front());
    _deferred.pop_front();
    handleEntityMessage(next.from, next.data, next.channel);
  }
}

void NetModule::receiveSamples(Tracked& t, const Values& delta, double time, int32_t origin) {
  // Their clock against ours: the least delay seen, relaxed slowly so drift can't stick.
  const double offset = _now - time;
  auto known = _clockOffset.find(origin);
  if (known == _clockOffset.end() || offset < known->second) {
    _clockOffset[origin] = offset;
  } else {
    known->second = std::min(offset, known->second + 0.0005);
  }
  t.origin = origin;
  if (!t.samples.empty() && time <= t.samples.back().time) return;  // older than what we have
  for (const auto& [key, bits] : delta) t.latest[key] = bits;
  // After a quiet spell (nothing changed, nothing sent) it was still where it was until just now.
  if (!t.samples.empty() && time - t.samples.back().time > 3 * _sendInterval) {
    t.samples.push_back({time - _sendInterval, t.samples.back().values});
  }
  t.samples.push_back({time, t.latest});
  while (t.samples.size() > 32) t.samples.pop_front();
}

void NetModule::handleEntityMessage(const Source& from, const std::vector<uint8_t>& data, uint8_t channel) {
  Reader r(data);
  const auto type = static_cast<Msg>(r.u8());
  SceneManager& scenes = _app->getSceneManager();
  World& world = _app->getWorld();
  // What a sender may change: the host anything; a player only what they own.
  auto mayChange = [&](const NetworkComponent* n) { return from.host || (n && n->owner == from.player); };
  auto relay = [&] {
    if (relays()) broadcast(data, type == Msg::State ? net::kUnreliable : channel, from.conn);
  };

  switch (type) {
    case Msg::Scene: {
      if (!from.host || isHost()) return;
      _epoch = r.u32();
      const std::string path = r.str();
      if (!r.ok()) return;
      _binds.clear();
      ++_sceneOpsPending;
      scenes.requestLoad(path);
      return;
    }
    case Msg::Group: {
      if (!from.host || isHost()) return;
      const uint32_t epoch = r.u32();
      const std::string group = r.str();
      const bool spawned = r.u8() != 0;
      if (!r.ok() || epoch != _epoch || scenes.groupSpawned(group) == spawned) return;
      ++_sceneOpsPending;
      scenes.requestGroup(group, spawned);
      return;
    }
    case Msg::Bind: {
      if (!from.host || isHost() || r.u32() != _epoch) return;
      const uint16_t count = r.u16();
      for (uint16_t i = 0; i < count && r.ok(); ++i) {
        std::string key = r.str();
        _binds[key] = r.u32();
      }
      applyBinds();
      return;
    }
    case Msg::Sync: {
      if (!from.host || isHost() || r.u32() != _epoch) return;
      const std::string prefix = r.str();
      applyBinds();
      // Entries the host doesn't have (destroyed there, or never built: an "if") go here too.
      for (const auto& [key, entity] : _keyed) {
        if (!key.starts_with(prefix) || _binds.contains(key)) continue;
        NetworkComponent* n = netOf(entity);
        if (!n || n->netId != 0 || n->gone) continue;
        n->gone = true;
        world.destroyDeferred(entity);
      }
      return;
    }
    case Msg::Spawn: {
      const uint32_t netId = r.u32();
      const int32_t owner = r.i32(), controller = r.i32();
      const std::string prefab = r.str();
      const float x = r.f32(), y = r.f32();
      nlohmann::json overrides = nlohmann::json::parse(r.str(), nullptr, false);
      const Values values = readValues(r);
      if (!r.ok() || (!from.host && owner != from.player)) return;
      if (auto known = _tracked.find(netId); known != _tracked.end()) {
        // Already here (the host's snapshot, then the owner's own news): just the latest.
        if (NetworkComponent* n = netOf(known->second.entity); n && !isMine(*n)) {
          n->owner = owner;
          n->controller = controller;
          for (const auto& [key, bits] : values) known->second.latest[key] = bits;
        }
        return;
      }
      if (!overrides.is_object()) overrides = nlohmann::json::object();
      const EntityId entity = _app->getSpawner().spawn(prefab, x, y, overrides);
      _incoming[entity] = Incoming{netId, owner, controller};
      Tracked& t = _tracked[netId];
      t.entity = entity;
      t.origin = owner;
      t.latest = values;
      t.announced = true;  // its owner told everyone (an owner that's us: the host spawned it for us)
      relay();
      return;
    }
    case Msg::Destroy: {
      const uint32_t netId = r.u32();
      auto it = _tracked.find(netId);
      if (!r.ok() || it == _tracked.end()) return;
      const NetworkComponent* n = netOf(it->second.entity);
      if (!mayChange(n) && !_incoming.contains(it->second.entity)) return;
      forget(netId);
      relay();
      return;
    }
    case Msg::State: {
      const int32_t origin = r.i32();
      const double time = r.f64();
      const uint16_t count = r.u16();
      for (uint16_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t netId = r.u32();
        const Values values = readValues(r);
        auto it = _tracked.find(netId);
        if (!r.ok() || it == _tracked.end()) continue;
        const NetworkComponent* n = netOf(it->second.entity);
        if (n && isMine(*n)) continue;
        if (n && !mayChange(n)) continue;
        receiveSamples(it->second, values, time, origin);
      }
      relay();
      return;
    }
    case Msg::Data: {
      const uint32_t netId = r.u32();
      const std::string key = r.str(), value = r.str();
      auto it = _tracked.find(netId);
      if (!r.ok() || it == _tracked.end()) return;
      const NetworkComponent* n = netOf(it->second.entity);
      if ((n && isMine(*n)) || (n && !mayChange(n))) return;
      const int32_t store = _app->getEntityStores().idFor(it->second.entity);
      if (GameState* state = _app->getEntityStores().find(store)) {
        if (value.empty()) {
          state->remove(key);
        } else if (auto json = nlohmann::json::parse(value, nullptr, false); !json.is_discarded()) {
          state->setJson(key, std::move(json));
        }
      }
      relay();
      return;
    }
    case Msg::Tags: {
      const uint32_t netId = r.u32();
      const uint16_t count = r.u16();
      std::vector<std::string> tags;
      for (uint16_t i = 0; i < count && r.ok(); ++i) tags.push_back(r.str());
      auto it = _tracked.find(netId);
      if (!r.ok() || it == _tracked.end()) return;
      const NetworkComponent* n = netOf(it->second.entity);
      if ((n && isMine(*n)) || (n && !mayChange(n))) return;
      const EntityId e = it->second.entity;
      for (const std::string& tag : world.tagNames(e)) {
        if (std::find(tags.begin(), tags.end(), tag) == tags.end()) world.removeTag(e, tag);
      }
      for (const std::string& tag : tags) world.addTag(e, tag);
      relay();
      return;
    }
    default:
      return;
  }
}

// ---- Copies between updates -----------------------------------------------------------

void NetModule::applyCopies(World& world) {
  if (!online()) return;
  for (auto& [id, t] : _tracked) {
    const NetworkComponent* n = netOf(t.entity);
    if (!n || n->gone || isMine(*n) || t.latest.empty()) continue;

    // Where it was `delay` ago by its owner's clock: between two samples, or the newest.
    const Values* from = &t.latest;
    const Values* to = nullptr;
    float alpha = 0.0f;
    if (n->interpolate && t.samples.size() >= 2) {
      const auto offset = _clockOffset.find(t.origin);
      const double at = _now - (offset == _clockOffset.end() ? 0.0 : offset->second) - _interpolationDelay;
      if (at <= t.samples.front().time) {
        from = &t.samples.front().values;
      } else if (at < t.samples.back().time) {
        for (size_t i = 0; i + 1 < t.samples.size(); ++i) {
          const Sample& a = t.samples[i];
          const Sample& b = t.samples[i + 1];
          if (at < b.time) {
            from = &a.values;
            to = &b.values;
            alpha = static_cast<float>((at - a.time) / std::max(1e-6, b.time - a.time));
            break;
          }
        }
      }
    }
    for (const auto& [key, bits] : *from) {
      const auto field = fieldOf(*n, key);
      if (!field) continue;
      uint32_t out = bits;
      const ScriptField& spec = field->component->scriptFields[field->index];
      if (to && !spec.integer) {
        if (auto next = to->find(key); next != to->end()) {
          float a, b;
          std::memcpy(&a, &bits, 4);
          std::memcpy(&b, &next->second, 4);
          const float v = spec.name == "rotation" ? lerpAngle(a, b, alpha) : a + (b - a) * alpha;
          std::memcpy(&out, &v, 4);
        }
      }
      world.writeScriptField(t.entity, *field, out);
    }
  }
}
