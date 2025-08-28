#include "HostFunctions.hpp"

#include "../app/Application.hpp"
#include "HostFunction.hpp"

static Application* currentApp = nullptr;

void setHostContext(Application& app) {
  currentApp = &app;
}

void clearHostContext() {
  currentApp = nullptr;
}

m3ApiRawFunction(log) {
  (void)_ctx;
  (void)_mem;
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  std::string message(reinterpret_cast<char*>(memory + ptr), len);
  std::cout << "[script] " << message << "\n";

  m3ApiSuccess();
}

m3ApiRawFunction(abort) {
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(int32_t, msg_ptr);
  m3ApiGetArg(int32_t, file_ptr);
  m3ApiGetArg(int32_t, line);
  m3ApiGetArg(int32_t, column);

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);

  std::string message(reinterpret_cast<char*>(memory + msg_ptr));
  std::string file(reinterpret_cast<char*>(memory + file_ptr));

  std::cerr << "[wasm abort] " << message << " at " << file << ":" << line << ":" << column << std::endl;

  m3ApiSuccess();
}

m3ApiRawFunction(ecsGetComponent) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(bool);
  m3ApiGetArg(uint32_t, entityId);
  m3ApiGetArg(uint32_t, entityGen);
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  if (!currentApp) {
    return "Application context missing";
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    return "Memory context missing";
  }

  std::string compName(reinterpret_cast<char*>(memory + ptr), len);

  std::cout << "[script] Component: " << compName << "\n";

  EntityId eid;
  eid.index = entityId;
  eid.generation = entityGen;
  // currentApp->getWorld().getComponent<???>(eid);
  // @TODO: getComponentByName(eid) ?
  // @TODO: Easy way to store entity id per script that can be accessed HERE
  // @TODO: Pass Component data in

  // m3ApiReturn(???);
  m3ApiReturn(false);
}

// @TODO: Create an update component host fn that will allow the script to
//        update a component and send it back to update the REAL component here
//
m3ApiRawFunction(ecsUpdateComponent) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(bool);
  m3ApiGetArg(uint32_t, entityId);
  m3ApiGetArg(uint32_t, entityGen);
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);
  // @TODO: arg for comp data

  if (!currentApp) {
    return "Application context missing";
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    return "Memory context missing";
  }

  std::string compName(reinterpret_cast<char*>(memory + ptr), len);

  m3ApiReturn(true);
}