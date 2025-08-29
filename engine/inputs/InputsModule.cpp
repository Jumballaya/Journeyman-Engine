#include "InputsModule.hpp"

#include "../core/app/Registration.hpp"
#include "../glfw_window/WindowEvents.hpp"
#include "InputsHostFunctions.hpp"

REGISTER_MODULE(InputsModule);

void InputsModule::initialize(Application& app) {
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

void InputsModule::shutdown(Application& app) {
  clearInputsHostContext();
  JM_LOG_INFO("[Inputs] shutdown");
}

void InputsModule::tickMainThread(Application& app, float dt) {
  _inputsManager.tick(dt);
}
