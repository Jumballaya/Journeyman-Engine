#include "InputsModule.hpp"

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

  // Events
  eventBus.subscribe<events::KeyDown>(EVT_KeyDown, [this](const events::KeyDown& e) {
    _inputsManager.registerKeyDown(_inputsManager.keyFromScancode(e.scancode));
  });

  eventBus.subscribe<events::KeyUp>(EVT_KeyUp, [this](const events::KeyUp& e) {
    _inputsManager.registerKeyUp(_inputsManager.keyFromScancode(e.scancode));
  });

  eventBus.subscribe<events::KeyRepeat>(EVT_KeyRepeat, [this](const events::KeyRepeat& e) {
    _inputsManager.registerKeyRepeat(_inputsManager.keyFromScancode(e.scancode));
  });

  // Scripting
  app.getScriptManager()
      .registerHostFunction("__jmKeyIsPressed", {"env", "__jmKeyIsPressed", "i(i)", &jmKeyIsPressed});
  app.getScriptManager()
      .registerHostFunction("__jmKeyIsReleased", {"env", "__jmKeyIsReleased", "i(i)", &jmKeyIsReleased});
  app.getScriptManager()
      .registerHostFunction("__jmKeyIsDown", {"env", "__jmKeyIsDown", "i(i)", &jmKeyIsDown});

  // Asset loading (.bindings.json)

  JM_LOG_INFO("[Inputs] initialized");
}

void InputsModule::shutdown(Engine& app) {
  clearInputsHostContext();
  JM_LOG_INFO("[Inputs] shutdown");
}

void InputsModule::tickMainThread(Engine& app, float dt) {
  _inputsManager.tick(dt);
}
