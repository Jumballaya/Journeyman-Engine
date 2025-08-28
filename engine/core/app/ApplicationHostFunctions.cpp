#include "ApplicationHostFunctions.hpp"

#include "../scripting/HostFunction.hpp"
#include "Application.hpp"

static Application* currentApp = nullptr;

void setHostContext(Application& app) {
  currentApp = &app;
}

void clearHostContext() {
  currentApp = nullptr;
}

m3ApiRawFunction(jmLog) {
  (void)_ctx;
  (void)_mem;
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  std::string message(reinterpret_cast<char*>(memory + ptr), len);
  std::cout << "[script] " << message << "\n";

  m3ApiSuccess();
}

m3ApiRawFunction(jmAbort) {
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

//
// ECS Scripting Error Codes:
//
// -1: no app/runtime/memory (fatal host state)
// -2: entity invalid/dead
// -3: component not registered for POD or name unknown
// -4: component missing on entity
// -5: buffer too small (outLen < podSize)
// -7: pointer/length out of memory range
// -8: serialize failed (adapter returned false)
//

m3ApiRawFunction(jmEcsGetComponent) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, outPtr);
  m3ApiGetArg(int32_t, outLen);

  if (!currentApp) {
    m3ApiReturn(-1);
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    m3ApiReturn(-1);
  }

  std::string compName(reinterpret_cast<char*>(memory + namePtr), nameLen);
  auto* ctx = static_cast<ScriptInstanceContext*>(m3_GetUserData(runtime));
  if (!ctx) {
    m3ApiReturn(-1);
  }
  auto& world = currentApp->getWorld();

  if (!world.isAlive(ctx->eid)) {
    m3ApiReturn(-2);
  }

  auto& compRegistry = world.getRegistry();
  auto compIdOpt = compRegistry.getComponentIdByName(compName);
  if (!compIdOpt.has_value()) {
    m3ApiReturn(-3);
  }

  auto compId = compIdOpt.value();
  auto info = compRegistry.getInfo(compId);
  if (!info || !info->podSerialize || info->podSize == 0) {
    m3ApiReturn(-4);
  }

  if (static_cast<size_t>(outLen) < info->podSize) {
    m3ApiReturn(-5);
  }

  size_t written = 0;
  std::span<std::byte> out{reinterpret_cast<std::byte*>(memory + outPtr), info->podSize};
  bool ok = info->podSerialize(world, ctx->eid, out, written);

  if (!ok || written != info->podSize) {
    m3ApiReturn(-8);
  }

  m3ApiReturn(static_cast<int32_t>(written));
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