#include "ApplicationHostFunctions.hpp"

#include <cstdint>

#include "../scripting/HostFunction.hpp"
#include "Application.hpp"

static Application* currentApp = nullptr;

void setHostContext(Application& app) {
  currentApp = &app;
}

void clearHostContext() {
  currentApp = nullptr;
}

namespace {
// Returns the length of a NUL-terminated string starting at `offset` inside
// WASM linear memory, without reading past `memSize`. Returns SIZE_MAX if no
// terminator is found within bounds.
size_t wasmStrnlen(const uint8_t* memory, size_t memSize, size_t offset) {
  if (offset >= memSize) return SIZE_MAX;
  size_t end = offset;
  while (end < memSize && memory[end] != 0) ++end;
  return (end < memSize) ? end - offset : SIZE_MAX;
}
}  // namespace

m3ApiRawFunction(jmLog) {
  (void)_ctx;
  (void)_mem;
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory) m3ApiSuccess();

  if (ptr < 0 || len < 0 ||
      static_cast<size_t>(ptr) + static_cast<size_t>(len) > memSize) {
    std::cerr << "[script] __jmLog: out-of-bounds pointer/length\n";
    m3ApiSuccess();
  }

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

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory) m3ApiSuccess();

  if (msg_ptr < 0 || file_ptr < 0) {
    std::cerr << "[wasm abort] (negative pointer arg)\n";
    m3ApiSuccess();
  }

  size_t msgLen = wasmStrnlen(memory, memSize, static_cast<size_t>(msg_ptr));
  size_t fileLen = wasmStrnlen(memory, memSize, static_cast<size_t>(file_ptr));
  if (msgLen == SIZE_MAX || fileLen == SIZE_MAX) {
    std::cerr << "[wasm abort] (unterminated/out-of-bounds string) line=" << line << " col=" << column << "\n";
    m3ApiSuccess();
  }

  std::string message(reinterpret_cast<char*>(memory + msg_ptr), msgLen);
  std::string file(reinterpret_cast<char*>(memory + file_ptr), fileLen);

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

  uint32_t memSize;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory) {
    m3ApiReturn(-1);
  }

  if (namePtr < 0 || nameLen < 0) {
    m3ApiReturn(-8);
  }
  if ((size_t)namePtr + (size_t)nameLen > memSize) {
    m3ApiReturn(-8);
  }
  if (outPtr < 0 || outLen < 0) {
    m3ApiReturn(-8);
  }
  if ((size_t)outPtr + (size_t)outLen > memSize) {
    m3ApiReturn(-8);
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

  auto& compRegistry = world.getComponentRegistry();
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

m3ApiRawFunction(jmEcsUpdateComponent) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, dataPtr);

  if (!currentApp) {
    m3ApiReturn(-1);
  }

  uint32_t memSize;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory) {
    m3ApiReturn(-1);
  }

  if (namePtr < 0 || nameLen < 0) {
    m3ApiReturn(-8);
  }
  if ((size_t)namePtr + (size_t)nameLen > memSize) {
    m3ApiReturn(-8);
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

  auto& compRegistry = world.getComponentRegistry();
  auto compIdOpt = compRegistry.getComponentIdByName(compName);
  if (!compIdOpt.has_value()) {
    m3ApiReturn(-3);
  }

  auto compId = compIdOpt.value();
  auto info = compRegistry.getInfo(compId);
  if (!info || !info->podDeserialize || info->podSize == 0) {
    m3ApiReturn(-4);
  }

  if (dataPtr < 0) {
    m3ApiReturn(-8);
  }
  if ((size_t)dataPtr + info->podSize > memSize) {
    m3ApiReturn(-8);
  }

  std::span<std::byte> data{reinterpret_cast<std::byte*>(memory + dataPtr), info->podSize};
  bool ok = info->podDeserialize(world, ctx->eid, data);

  if (!ok) {
    m3ApiReturn(-8);
  }

  m3ApiReturn(ok ? info->podSize : 0);
}