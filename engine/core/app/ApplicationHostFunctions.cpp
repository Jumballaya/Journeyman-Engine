#include "ApplicationHostFunctions.hpp"

#include <cstdint>

#include "../logger/logging.hpp"
#include "../scripting/HostFunction.hpp"
#include "../scripting/WasmMemory.hpp"
#include "Engine.hpp"

static Engine* currentEngine = nullptr;

void setHostContext(Engine& engine) {
  currentEngine = &engine;
}

void clearHostContext() {
  currentEngine = nullptr;
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
  JM_LOG_INFO("[script] {}", message);

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
  JM_LOG_ERROR("[wasm abort] {} at {}:{}:{}", message, file, line, column);

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
// -8: pointer/length out of memory range, or serialize failed
//

namespace {

const ComponentInfo* podInfo(World& world, const std::string& name) {
  auto id = world.getComponentRegistry().getComponentIdByName(name);
  if (!id.has_value()) return nullptr;
  const ComponentInfo* info = world.getComponentRegistry().getInfo(*id);
  return (info && info->podSize > 0) ? info : nullptr;
}

int32_t readComponent(IM3Runtime runtime, EntityId eid, int32_t namePtr, int32_t nameLen,
                      int32_t outPtr, int32_t outLen) {
  if (!currentEngine) return -1;
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  uint8_t* out = wasm_memory::span(runtime, outPtr, outLen);
  if (!name || !out) return -8;

  World& world = currentEngine->getWorld();
  if (!world.isAlive(eid)) return -2;
  const ComponentInfo* info = podInfo(world, *name);
  if (!info || !info->podSerialize) return -3;
  if (static_cast<size_t>(outLen) < info->podSize) return -5;

  size_t written = 0;
  std::span<std::byte> dst{reinterpret_cast<std::byte*>(out), info->podSize};
  if (!info->podSerialize(world, eid, dst, written)) return -4;
  return written == info->podSize ? static_cast<int32_t>(written) : -8;
}

int32_t writeComponent(IM3Runtime runtime, EntityId eid, int32_t namePtr, int32_t nameLen,
                       int32_t dataPtr) {
  if (!currentEngine) return -1;
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  if (!name) return -8;

  World& world = currentEngine->getWorld();
  if (!world.isAlive(eid)) return -2;
  const ComponentInfo* info = podInfo(world, *name);
  if (!info || !info->podDeserialize) return -3;
  uint8_t* data = wasm_memory::span(runtime, dataPtr, static_cast<int32_t>(info->podSize));
  if (!data) return -8;

  std::span<const std::byte> src{reinterpret_cast<const std::byte*>(data), info->podSize};
  return info->podDeserialize(world, eid, src) ? static_cast<int32_t>(info->podSize) : -4;
}

EntityId selfOf(IM3Runtime runtime) {
  auto* ctx = static_cast<ScriptInstanceContext*>(m3_GetUserData(runtime));
  return ctx ? ctx->eid : EntityId{UINT32_MAX, UINT32_MAX};
}

}  // namespace

m3ApiRawFunction(jmEcsGetComponent) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, outPtr);
  m3ApiGetArg(int32_t, outLen);
  m3ApiReturn(readComponent(runtime, selfOf(runtime), namePtr, nameLen, outPtr, outLen));
}

m3ApiRawFunction(jmEcsUpdateComponent) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, dataPtr);
  m3ApiReturn(writeComponent(runtime, selfOf(runtime), namePtr, nameLen, dataPtr));
}

m3ApiRawFunction(jmEcsGetComponentOf) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, index);
  m3ApiGetArg(int32_t, generation);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, outPtr);
  m3ApiGetArg(int32_t, outLen);
  EntityId eid{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
  m3ApiReturn(readComponent(runtime, eid, namePtr, nameLen, outPtr, outLen));
}

m3ApiRawFunction(jmEcsUpdateComponentOf) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, index);
  m3ApiGetArg(int32_t, generation);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, dataPtr);
  EntityId eid{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
  m3ApiReturn(writeComponent(runtime, eid, namePtr, nameLen, dataPtr));
}
