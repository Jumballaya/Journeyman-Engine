#include "InputsModule.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../glfw_window/WindowEvents.hpp"
#include "InputsHostFunctions.hpp"

// Inputs subscribes to window key events, so a window has to exist before
// Inputs initializes.
template <>
struct ModuleTraits<InputsModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<WindowTag>;
};

REGISTER_MODULE(InputsModule);

void InputsModule::initialize(Engine& app) {
  setInputsHostContext(app, *this);

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

  registerInputsHostFunctions(app.getScriptManager());

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

  if (const char* replay = std::getenv("JM_INPUT_REPLAY")) loadReplay(replay);

  JM_LOG_INFO("[Inputs] initialized");
}

void InputsModule::shutdown(Engine& app) {
  clearInputsHostContext();
  JM_LOG_INFO("[Inputs] shutdown");
}

void InputsModule::tickMainThread(Engine& app, float dt) {
  // Clears last frame's pressed/released edges; key events queued this frame
  // are applied when the event bus dispatches, after this tick.
  _inputsManager.tick(dt);
  _actions.pollGamepads();
  applyReplay();
  ++_frame;
}

void InputsModule::loadReplay(const char* path) {
  std::ifstream in(path);
  if (!in) {
    JM_LOG_ERROR("[Inputs] JM_INPUT_REPLAY: cannot open '{}'", path);
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
  JM_LOG_INFO("[Inputs] replaying {} input events from {}", _replay.size(), path);
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
