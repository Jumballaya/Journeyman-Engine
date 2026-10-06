#include "InputsModule.hpp"

#include <fstream>
#include <sstream>

#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/app/WindowEvents.hpp"

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

  eventBus.subscribe<events::KeyDown>(EVT_KeyDown, [this](const events::KeyDown& e) {
    _inputsManager.registerKeyDown(_inputsManager.keyFromEvent(e.scancode, e.key));
  });
  eventBus.subscribe<events::KeyUp>(EVT_KeyUp, [this](const events::KeyUp& e) {
    _inputsManager.registerKeyUp(_inputsManager.keyFromEvent(e.scancode, e.key));
  });
  eventBus.subscribe<events::KeyRepeat>(EVT_KeyRepeat, [this](const events::KeyRepeat& e) {
    _inputsManager.registerKeyRepeat(_inputsManager.keyFromEvent(e.scancode, e.key));
  });
  eventBus.subscribe<events::MouseButton>(EVT_MouseButton, [this](const events::MouseButton& e) {
    if (e.button < 0 || e.button > 2) return;
    const auto key = static_cast<inputs::Key>(inputs::Key::MouseLeft + e.button);
    if (e.down) _inputsManager.registerKeyDown(key);
    else _inputsManager.registerKeyUp(key);
  });
  eventBus.subscribe<events::MouseWheel>(EVT_MouseWheel, [this](const events::MouseWheel& e) { _inputsManager.registerWheel(e.dx, e.dy); });

  bindScriptApi(app.getScriptManager());

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

  JM_LOG_INFO("[Inputs] initialized");
}

void InputsModule::shutdown(Engine&) {
  JM_LOG_INFO("[Inputs] shutdown");
}

void InputsModule::bindScriptApi(ScriptManager& s) {
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
  _actions.pollGamepads(dt);
  applyReplay();
  ++_frame;
}

void InputsModule::loadReplay(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    JM_LOG_ERROR("[Inputs] JM_INPUT_REPLAY: cannot open '{}'", path.string());
    return;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    std::istringstream fields(line);
    uint64_t frame;
    std::string action, keyName;
    if (!(fields >> frame >> action >> keyName)) continue;
    auto control = inputs::parseControl(keyName);
    if (!control || !std::holds_alternative<inputs::Key>(*control) || (action != "down" && action != "up")) {
      JM_LOG_WARN("[Inputs] replay: skipping '{}'", line);
      continue;
    }
    _replay.push_back({frame, action == "down", std::get<inputs::Key>(*control)});
  }
  std::stable_sort(_replay.begin(), _replay.end(),
                   [](const ReplayEvent& a, const ReplayEvent& b) { return a.frame < b.frame; });
  JM_LOG_INFO("[Inputs] replaying {} input events from {}", _replay.size(), path.string());
}

void InputsModule::applyReplay() {
  while (_replayCursor < _replay.size() && _replay[_replayCursor].frame <= _frame) {
    const auto& e = _replay[_replayCursor++];
    if (e.down) {
      _inputsManager.registerKeyDown(e.key);
    } else {
      _inputsManager.registerKeyUp(e.key);
    }
  }
}
