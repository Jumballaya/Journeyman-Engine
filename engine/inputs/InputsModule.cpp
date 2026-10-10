#include "InputsModule.hpp"

#include <fstream>
#include <sstream>

#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/app/WindowEvents.hpp"
#include "Devices.hpp"
#include "Replay.hpp"

template <>
struct ModuleTraits<InputsModule> {
  using Provides = TypeList<InputsTag>;
  using DependsOn = TypeList<>;
};

REGISTER_MODULE(InputsModule);

using host::ScriptCall;

void InputsModule::setKey(Engine& app, inputs::Key key, bool down) {
  if (key >= inputs::Key::Key_Count || _inputsManager.keyIsDown(key) == down) return;  // a repeat, or no change
  if (down) _inputsManager.registerKeyDown(key);
  else _inputsManager.registerKeyUp(key);
  // A play session keeps keys by name: a scancode means another key on
  // another machine.
  app.recordInput({{"type", "key"}, {"name", std::string(inputs::keyName(key))}, {"down", down}});
  if (down && key == inputs::Key::F8 && !app.devicesMuted()) app.dropMarker();
}

void InputsModule::initialize(Engine& app) {
  EventBus& eventBus = app.getEventBus();

  // Device events, delivered at the end of the frame they came in. A driven
  // or replayed run's devices are muted where their events start (the window
  // module), so a run is the same however the machine's devices behave.
  auto keyDown = [this, &app](const auto& e) {
    setKey(app, inputs::devices::keyFromEvent(e.scancode, e.key), true);
  };
  eventBus.subscribe<events::KeyDown>(EVT_KeyDown, keyDown);
  eventBus.subscribe<events::KeyRepeat>(EVT_KeyRepeat, keyDown);
  eventBus.subscribe<events::KeyUp>(EVT_KeyUp, [this, &app](const events::KeyUp& e) {
    setKey(app, inputs::devices::keyFromEvent(e.scancode, e.key), false);
  });
  // A replayed play's keys, by name: delivered as the player's were.
  eventBus.subscribe<events::NamedKey>(EVT_NamedKey, [this, &app](const events::NamedKey& e) {
    const auto control = inputs::parseControl(e.name);
    if (control && std::holds_alternative<inputs::Key>(*control)) setKey(app, std::get<inputs::Key>(*control), e.down);
  });
  eventBus.subscribe<events::MouseButton>(EVT_MouseButton, [this](const events::MouseButton& e) {
    if (e.button < 0 || e.button > 2) return;
    const auto key = static_cast<inputs::Key>(inputs::Key::MouseLeft + e.button);
    if (e.down) _inputsManager.registerKeyDown(key);
    else _inputsManager.registerKeyUp(key);
  });
  eventBus.subscribe<events::MouseWheel>(EVT_MouseWheel, [this](const events::MouseWheel& e) {
    _inputsManager.registerWheel(e.dx, e.dy);
  });

  // Action bindings: any .bindings.json asset (usually listed in the manifest
  // so it preloads) merges into the action map.
  auto bindingsDecoder = [this](const RawAsset& asset, const AssetHandle&) {
    nlohmann::json json = nlohmann::json::parse(asset.data.begin(), asset.data.end(), nullptr, false);
    if (json.is_discarded()) {
      JM_LOG_ERROR("[Inputs] {}: invalid JSON", asset.filePath.string());
      return;
    }
    _actions.loadBindings(json, asset.filePath.string());
    JM_LOG_INFO("[Inputs] loaded bindings from {}", asset.filePath.string());
  };
  app.getAssetManager().addAssetConverter({".bindings.json"}, bindingsDecoder);
  app.getAssetManager().addAssetTypeConverter("bindings", bindingsDecoder);

  if (!app.getDevOptions().inputReplay.empty()) loadReplay(app.getDevOptions().inputReplay);
  _driven = app.getDevOptions().drive;
  if (_driven && !app.getDevOptions().driveRecord.empty()) {
    _record.open(app.getDevOptions().driveRecord, std::ios::trunc);
    if (_record) _record << "# Inputs of a driven run (JM_DRIVE); play it back with JM_INPUT_REPLAY.\n";
    else JM_LOG_ERROR("[Inputs] JM_DRIVE_RECORD: cannot write '{}'", app.getDevOptions().driveRecord.string());
  }

  JM_LOG_INFO("[Inputs] initialized");
}

void InputsModule::shutdown(Engine&) {
  JM_LOG_INFO("[Inputs] shutdown");
}

void InputsModule::bindScriptApi(Engine& app) {
  ScriptManager& s = app.getScriptManager();
  // Raw keys (inputs::Key order, mirrored by the Key enum in the runtime).
  // query: 0 = down, 1 = pressed this frame, 2 = released this frame.
  // A script on an entity a remote player controls (multiplayer) reads that
  // player's input; every other script reads this machine's devices.
  s.bind("__jmKeyState", [this](ScriptCall& call, int32_t key, int32_t query) {
    if (key < 0 || key >= inputs::Key::Key_Count) return false;
    const auto k = static_cast<inputs::Key>(key);
    const RemoteInput* remote = remoteFor(call.self());
    const InputsManager& keys = remote ? remote->keys() : _inputsManager;
    return query == 1 ? keys.keyIsPressed(k)
         : query == 2 ? keys.keyIsReleased(k)
                      : keys.keyIsDown(k);
  });
  s.bind("__jmActionState", [this](ScriptCall& call, std::string action, int32_t query) {
    if (const RemoteInput* remote = remoteFor(call.self())) {
      return query == 1 ? remote->justPressed(action) : query == 2 ? remote->justReleased(action) : remote->down(action);
    }
    return query == 1 ? _actions.justPressed(action, _inputsManager)
         : query == 2 ? _actions.justReleased(action, _inputsManager)
                      : _actions.down(action, _inputsManager);
  });
  s.bind("__jmActionValue", [this](ScriptCall& call, std::string action) {
    if (const RemoteInput* remote = remoteFor(call.self())) return remote->value(action);
    return _actions.value(action, _inputsManager);
  });
  s.bind("__jmActionRepeated", [this](ScriptCall& call, std::string action, float delay, float interval) {
    if (const RemoteInput* remote = remoteFor(call.self())) return remote->repeated(action, delay, interval);
    return _actions.repeated(action, _inputsManager, delay, interval);
  });
  // The last frame's scroll: x right, y up.
  s.bind("__jmMouseWheel", [this](ScriptCall& call, int32_t axis) {
    const RemoteInput* remote = remoteFor(call.self());
    const glm::vec2 wheel = remote ? remote->keys().wheel() : _inputsManager.wheel();
    return axis == 0 ? wheel.x : wheel.y;
  });
  s.bind("__jmActionBind", [this](std::string action, std::string control) { return _actions.bind(action, control); });
  s.bind("__jmActionUnbind", [this](std::string action) { _actions.unbind(action); });
  s.bind("__jmGamepadConnected", [this]() { return _actions.gamepadConnected(); });
}

void InputsModule::tickMainThread(Engine& app, float dt) {
  // Clears last frame's pressed/released edges; key events queued this frame
  // are applied when the event bus dispatches, after this tick (and remote
  // players' snapshots when the net module ticks, after this one).
  _inputsManager.tick(dt);
  for (auto& [player, remote] : _remote) remote.tick(dt);
  // A replay handing over to the player (JM_PLAY_THEN=live): what it held
  // isn't held by them.
  if (_wasMuted && !app.devicesMuted()) releaseAll(app);
  _wasMuted = app.devicesMuted();
  // Pads are read, not evented: a recorded session notes one was there (its
  // replay can't repeat what it did).
  if (!app.devicesMuted() && app.getDevOptions().renderer != "none") {
    const auto pads = inputs::devices::readGamepads();
    if (!pads.empty()) app.noteGamepadUsed();
    _actions.applyGamepads(pads, dt);
  }
  applyReplay(app);
  ++_frame;
}

const RemoteInput* InputsModule::remoteFor(EntityId entity) const {
  if (!_controllerOf) return nullptr;
  const int32_t player = _controllerOf(entity);
  if (player < 0) return nullptr;
  auto it = _remote.find(player);
  return it == _remote.end() ? &_idle : &it->second;
}

InputSnapshot InputsModule::localSnapshot() const {
  InputSnapshot snapshot;
  for (uint16_t k = 0; k < inputs::Key::Key_Count; ++k) {
    const auto key = static_cast<inputs::Key>(k);
    if (_inputsManager.keyIsDown(key)) snapshot.setKey(key, true);
  }
  for (const std::string& name : _actions.actionNames()) {
    snapshot.actions.push_back({name, _actions.down(name, _inputsManager), _actions.value(name, _inputsManager)});
  }
  return snapshot;
}

bool InputsModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if (verb != "down" && verb != "up" && verb != "press") return false;
  const auto control = inputs::parseControl(args);
  if (!control || !std::holds_alternative<inputs::Key>(*control)) {
    reply = {{"ok", false}, {"error", "unknown key '" + std::string(args) + "' (names as in replay files: Space, Enter, A, ArrowLeft...)"}};
    return true;
  }
  const inputs::Key key = std::get<inputs::Key>(*control);
  // Applied now, the next frame's systems see it: in a replay that's an event
  // on the frame before (frame 0 at the start, the earliest a replay can say).
  const uint64_t replayFrame = _frame > 0 ? _frame - 1 : 0;
  auto record = [&](uint64_t frame, bool down) {
    if (_record) _record << frame << (down ? " down " : " up ") << args << std::endl;
  };
  setKey(app, key, verb != "up");
  record(replayFrame, verb != "up");
  if (verb == "press") {  // released after the next frame
    _replay.push_back({_frame, false, key});
    record(_frame, false);
  }
  reply = {{"ok", true}};
  return true;
}

void InputsModule::loadReplay(const std::filesystem::path& path) {
  _replayFile = path;
  std::ifstream in(path);
  if (!in) {
    JM_LOG_ERROR("[Inputs] JM_INPUT_REPLAY: cannot open '{}'", path.string());
    return;
  }
  std::vector<std::string> skipped;
  _replay = inputs::parseReplay(in, &skipped);
  for (const std::string& line : skipped) JM_LOG_WARN("[Inputs] replay: skipping '{}'", line);
  JM_LOG_INFO("[Inputs] replaying {} input events from {}", _replay.size(), path.string());
}

void InputsModule::releaseAll(Engine& app) {
  for (int button = 0; button < 3; ++button) {
    if (_inputsManager.keyIsDown(static_cast<inputs::Key>(inputs::Key::MouseLeft + button))) {
      app.getEventBus().emit(EVT_MouseButton, events::MouseButton{button, false});
    }
  }
  for (uint16_t k = 0; k < inputs::Key::MouseLeft; ++k) setKey(app, static_cast<inputs::Key>(k), false);
}

void InputsModule::applyReplay(Engine& app) {
  while (_replayCursor < _replay.size() && _replay[_replayCursor].frame <= _frame) {
    const auto& e = _replay[_replayCursor++];
    setKey(app, e.key, e.down);
  }
}
