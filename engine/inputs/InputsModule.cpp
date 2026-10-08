#include "InputsModule.hpp"

#include <fstream>
#include <sstream>

#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/app/WindowEvents.hpp"
#include "Replay.hpp"

// Inputs subscribes to window key events, so a window has to exist before
// Inputs initializes.
template <>
struct ModuleTraits<InputsModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<WindowTag>;
};

REGISTER_MODULE(InputsModule);

void InputsModule::initialize(Engine& app) {
  EventBus& eventBus = app.getEventBus();
  _inputsManager.initialize(eventBus);

  // During a replay the devices are ignored: the file is the only input, so
  // a run is the same however the machine's keyboard, mouse or pads behave.
  auto keyDown = [this](const auto& e) {
    if (!replaying()) _inputsManager.registerKeyDown(_inputsManager.keyFromEvent(e.scancode, e.key));
  };
  eventBus.subscribe<events::KeyDown>(EVT_KeyDown, keyDown);
  eventBus.subscribe<events::KeyRepeat>(EVT_KeyRepeat, keyDown);
  eventBus.subscribe<events::KeyUp>(EVT_KeyUp, [this](const events::KeyUp& e) {
    if (!replaying()) _inputsManager.registerKeyUp(_inputsManager.keyFromEvent(e.scancode, e.key));
  });
  eventBus.subscribe<events::MouseButton>(EVT_MouseButton, [this](const events::MouseButton& e) {
    if (replaying() || e.button < 0 || e.button > 2) return;
    const auto key = static_cast<inputs::Key>(inputs::Key::MouseLeft + e.button);
    if (e.down) _inputsManager.registerKeyDown(key);
    else _inputsManager.registerKeyUp(key);
  });
  eventBus.subscribe<events::MouseWheel>(EVT_MouseWheel, [this](const events::MouseWheel& e) {
    if (!replaying()) _inputsManager.registerWheel(e.dx, e.dy);
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
  s.bind("__jmKeyState", [this](int32_t key, int32_t query) {
    if (key < 0 || key >= inputs::Key::Key_Count) return false;
    const auto k = static_cast<inputs::Key>(key);
    return query == 1 ? _inputsManager.keyIsPressed(k)
         : query == 2 ? _inputsManager.keyIsReleased(k)
                      : _inputsManager.keyIsDown(k);
  });
  s.bind("__jmActionState", [this](std::string action, int32_t query) {
    return query == 1 ? _actions.pressed(action, _inputsManager)
         : query == 2 ? _actions.released(action, _inputsManager)
                      : _actions.down(action, _inputsManager);
  });
  s.bind("__jmActionValue", [this](std::string action) { return _actions.value(action, _inputsManager); });
  s.bind("__jmActionRepeated", [this](std::string action, float delay, float interval) {
    return _actions.repeated(action, _inputsManager, delay, interval);
  });
  // The last frame's scroll: x right, y up.
  s.bind("__jmMouseWheel", [this](int32_t axis) { return axis == 0 ? _inputsManager.wheel().x : _inputsManager.wheel().y; });
  s.bind("__jmActionBind", [this](std::string action, std::string control) { return _actions.bind(action, control); });
  s.bind("__jmActionUnbind", [this](std::string action) { _actions.unbind(action); });
  s.bind("__jmGamepadConnected", [this]() { return _actions.gamepadConnected(); });
}

void InputsModule::tickMainThread(Engine& app, float dt) {
  // Clears last frame's pressed/released edges; key events queued this frame
  // are applied when the event bus dispatches, after this tick.
  _inputsManager.tick(dt);
  if (!replaying() && app.getDevOptions().renderer != "none") _actions.pollGamepads(dt);  // GLFW reads them
  applyReplay();
  ++_frame;
}

bool InputsModule::driveCommand(Engine&, std::string_view verb, std::string_view args, nlohmann::json& reply) {
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
  if (verb == "up") {
    _inputsManager.registerKeyUp(key);
    record(replayFrame, false);
  } else {
    _inputsManager.registerKeyDown(key);
    record(replayFrame, true);
  }
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

void InputsModule::applyReplay() {
  while (_replayCursor < _replay.size() && _replay[_replayCursor].frame <= _frame) {
    const auto& e = _replay[_replayCursor++];
    e.down ? _inputsManager.registerKeyDown(e.key) : _inputsManager.registerKeyUp(e.key);
  }
}
