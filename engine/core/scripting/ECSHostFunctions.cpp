#include "ECSHostFunctions.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../app/Application.hpp"
#include "../ecs/component/ComponentRegistry.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "../ecs/World.hpp"
#include "HostFunctions.hpp"  // For getCurrentApp()

// Access currentApp from HostFunctions - it's static there, so we need a getter
// For now, we'll access it via a helper that matches the pattern
// Actually, we can just include HostFunctions.hpp and the static will be accessible
// But static in different translation unit won't work. Let's use the same pattern.

// Helper to get World from current application context
static World* getWorld() {
  Application* app = getCurrentApp();
  if (!app) {
    return nullptr;
  }
  return &app->getWorld();
}

// Helper to construct EntityId from index and generation
static EntityId makeEntityId(uint32_t index, uint32_t generation) {
  EntityId id;
  id.index = index;
  id.generation = generation;
  return id;
}

// Helper to write EntityId to WASM memory
static void writeEntityId(IM3Runtime runtime, uint32_t indexOutPtr, uint32_t generationOutPtr, const EntityId& id) {
  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) return;

  *reinterpret_cast<uint32_t*>(memory + indexOutPtr) = id.index;
  *reinterpret_cast<uint32_t*>(memory + generationOutPtr) = id.generation;
}

// Helper to read string from WASM memory
static std::string readString(IM3Runtime runtime, uint32_t ptr, uint32_t len) {
  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) return "";

  return std::string(reinterpret_cast<char*>(memory + ptr), len);
}

// ============================================================================
// Entity Management Functions
// ============================================================================

m3ApiRawFunction(createEntity) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, indexOutPtr);
  m3ApiGetArg(uint32_t, generationOutPtr);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();  // Fail silently or could return error
    return;
  }

  EntityId id = world->createEntity();
  writeEntityId(runtime, indexOutPtr, generationOutPtr, id);

  m3ApiSuccess();
}

m3ApiRawFunction(destroyEntity) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (world->isAlive(id)) {
    world->destroyEntity(id);
  }

  m3ApiSuccess();
}

m3ApiRawFunction(isEntityAlive) {
  m3ApiReturnType(int32_t);
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);

  World* world = getWorld();
  if (!world) {
    m3ApiReturn(0);
    return;
  }

  EntityId id = makeEntityId(index, generation);
  int32_t result = world->isAlive(id) ? 1 : 0;
  m3ApiReturn(result);
}

m3ApiRawFunction(cloneEntity) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, srcIndex);
  m3ApiGetArg(uint32_t, srcGeneration);
  m3ApiGetArg(uint32_t, dstIndexOutPtr);
  m3ApiGetArg(uint32_t, dstGenerationOutPtr);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();
    return;
  }

  EntityId srcId = makeEntityId(srcIndex, srcGeneration);
  if (!world->isAlive(srcId)) {
    // Write invalid entity ID (0, 0)
    writeEntityId(runtime, dstIndexOutPtr, dstGenerationOutPtr, EntityId{0, 0});
    m3ApiSuccess();
    return;
  }

  EntityId dstId = world->cloneEntity(srcId);
  writeEntityId(runtime, dstIndexOutPtr, dstGenerationOutPtr, dstId);

  m3ApiSuccess();
}

// ============================================================================
// Component Operations
// ============================================================================

m3ApiRawFunction(hasComponent) {
  m3ApiReturnType(int32_t);
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, componentNamePtr);
  m3ApiGetArg(uint32_t, componentNameLen);

  World* world = getWorld();
  if (!world) {
    m3ApiReturn(0);
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiReturn(0);
    return;
  }

  std::string componentName = readString(runtime, componentNamePtr, componentNameLen);
  auto maybeComponentId = world->getRegistry().getComponentIdByName(componentName);
  if (!maybeComponentId.has_value()) {
    m3ApiReturn(0);
    return;
  }

  ComponentId componentId = maybeComponentId.value();
  const ComponentInfo* info = world->getRegistry().getInfo(componentId);
  if (!info) {
    m3ApiReturn(0);
    return;
  }

  // Check if component exists using World helper
  ComponentId componentId = maybeComponentId.value();
  int32_t result = world->hasComponentById(id, componentId) ? 1 : 0;
  m3ApiReturn(result);
}

m3ApiRawFunction(getComponentHandle) {
  m3ApiReturnType(int32_t);
  (void)_ctx;
  (void)_mem;

  // Same implementation as hasComponent for now
  // Future: Could return actual component handle
  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, componentNamePtr);
  m3ApiGetArg(uint32_t, componentNameLen);

  World* world = getWorld();
  if (!world) {
    m3ApiReturn(0);
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiReturn(0);
    return;
  }

  std::string componentName = readString(runtime, componentNamePtr, componentNameLen);
  auto maybeComponentId = world->getRegistry().getComponentIdByName(componentName);
  if (!maybeComponentId.has_value()) {
    m3ApiReturn(0);
    return;
  }

  // Check if component exists (same as hasComponent)
  ComponentId componentId = maybeComponentId.value();
  int32_t result = world->hasComponentById(id, componentId) ? 1 : 0;
  m3ApiReturn(result);
}

m3ApiRawFunction(addComponentFromJSON) {
  m3ApiReturnType(int32_t);
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, componentNamePtr);
  m3ApiGetArg(uint32_t, componentNameLen);
  m3ApiGetArg(uint32_t, jsonPtr);
  m3ApiGetArg(uint32_t, jsonLen);

  World* world = getWorld();
  if (!world) {
    m3ApiReturn(0);
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiReturn(0);
    return;
  }

  std::string componentName = readString(runtime, componentNamePtr, componentNameLen);
  std::string jsonString = readString(runtime, jsonPtr, jsonLen);

  auto maybeComponentId = world->getRegistry().getComponentIdByName(componentName);
  if (!maybeComponentId.has_value()) {
    m3ApiReturn(0);
    return;
  }

  ComponentId componentId = maybeComponentId.value();
  const ComponentInfo* info = world->getRegistry().getInfo(componentId);
  if (!info || !info->deserializeFn) {
    m3ApiReturn(0);
    return;
  }

  try {
    nlohmann::json componentJson = nlohmann::json::parse(jsonString);
    info->deserializeFn(*world, id, componentJson);
    m3ApiReturn(1);
  } catch (const std::exception&) {
    m3ApiReturn(0);
  }
}

m3ApiRawFunction(removeComponent) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, componentNamePtr);
  m3ApiGetArg(uint32_t, componentNameLen);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiSuccess();
    return;
  }

  std::string componentName = readString(runtime, componentNamePtr, componentNameLen);
  auto maybeComponentId = world->getRegistry().getComponentIdByName(componentName);
  if (!maybeComponentId.has_value()) {
    m3ApiSuccess();
    return;
  }

  ComponentId componentId = maybeComponentId.value();
  world->removeComponentById(id, componentId);

  m3ApiSuccess();
}

// ============================================================================
// Tag Operations
// ============================================================================

m3ApiRawFunction(addTag) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, tagPtr);
  m3ApiGetArg(uint32_t, tagLen);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiSuccess();
    return;
  }

  std::string tag = readString(runtime, tagPtr, tagLen);
  world->addTag(id, tag);

  m3ApiSuccess();
}

m3ApiRawFunction(removeTag) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, tagPtr);
  m3ApiGetArg(uint32_t, tagLen);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiSuccess();
    return;
  }

  std::string tag = readString(runtime, tagPtr, tagLen);
  world->removeTag(id, tag);

  m3ApiSuccess();
}

m3ApiRawFunction(hasTag) {
  m3ApiReturnType(int32_t);
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);
  m3ApiGetArg(uint32_t, tagPtr);
  m3ApiGetArg(uint32_t, tagLen);

  World* world = getWorld();
  if (!world) {
    m3ApiReturn(0);
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiReturn(0);
    return;
  }

  std::string tag = readString(runtime, tagPtr, tagLen);
  int32_t result = world->hasTag(id, tag) ? 1 : 0;
  m3ApiReturn(result);
}

m3ApiRawFunction(clearTags) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;

  m3ApiGetArg(uint32_t, index);
  m3ApiGetArg(uint32_t, generation);

  World* world = getWorld();
  if (!world) {
    m3ApiSuccess();
    return;
  }

  EntityId id = makeEntityId(index, generation);
  if (!world->isAlive(id)) {
    m3ApiSuccess();
    return;
  }

  world->clearTags(id);

  m3ApiSuccess();
}

m3ApiRawFunction(findEntitiesWithTag) {
  m3ApiReturnType(int32_t);
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(uint32_t, tagPtr);
  m3ApiGetArg(uint32_t, tagLen);
  m3ApiGetArg(uint32_t, resultIndexArrayPtr);
  m3ApiGetArg(uint32_t, resultGenArrayPtr);
  m3ApiGetArg(uint32_t, arrayCapacity);

  World* world = getWorld();
  if (!world) {
    m3ApiReturn(0);
    return;
  }

  std::string tag = readString(runtime, tagPtr, tagLen);
  auto entities = world->findWithTag(tag);

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    m3ApiReturn(0);
    return;
  }

  uint32_t* indexArray = reinterpret_cast<uint32_t*>(memory + resultIndexArrayPtr);
  uint32_t* genArray = reinterpret_cast<uint32_t*>(memory + resultGenArrayPtr);

  uint32_t count = 0;
  uint32_t capacity = arrayCapacity;
  for (const EntityId& id : entities) {
    if (count >= capacity) break;
    indexArray[count] = id.index;
    genArray[count] = id.generation;
    count++;
  }

  m3ApiReturn(static_cast<int32_t>(count));
}
