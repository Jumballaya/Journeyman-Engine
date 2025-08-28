#include "InputsModule.hpp"

#include "../core/app/Registration.hpp"
#include "InputsHostFunctions.hpp"

REGISTER_MODULE(InputsModule);

void InputsModule::initialize(Application& app) {
  setInputsHostContext(app, *this);

  // Events

  // Scripting
  app.getScriptManager()
      .registerHostFunction("__jmKeyIsPressed", {"env", "__jmKeyIsPressed", "i(ii)", &keyIsPressed});

  // Asset loading (.bindings.json)

  JM_LOG_INFO("[Inputs] initialized");
}

void InputsModule::shutdown(Application& app) {
  clearInputsHostContext();
  JM_LOG_INFO("[Inputs] shutdown");
}

void InputsModule::tickMainThread(Application& app, float dt) {}
