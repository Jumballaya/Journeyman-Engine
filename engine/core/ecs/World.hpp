#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../tasks/TaskGraph.hpp"
#include "ECSRegistry.hpp"
#include "View.hpp"
#include "archetype/Archetype.hpp"
#include "archetype/ArchetypeSet.hpp"
#include "archetype/ArchetypeSignature.hpp"
#include "component/ComponentConcepts.hpp"
#include "component/ComponentInfo.hpp"
#include "component/ComponentRegistry.hpp"
#include "entity/EntityBuilder.hpp"
#include "entity/EntityId.hpp"
#include "entity/EntityManager.hpp"
#include "entity/EntityRef.hpp"
#include "entity/TagSymbol.hpp"
#include "system/SystemScheduler.hpp"

struct EntityRecord {
  Archetype* archetype = nullptr;
  uint32_t row = 0;
};

class World {
 public:
  World() = default;
  ~World() = default;
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept = default;
  World& operator=(World&&) noexcept = default;

  EntityRef operator[](EntityId id);

  void buildExecutionGraph(TaskGraph& graph, float dt);

  template <ComponentType... Ts>
  View<Ts...> view();

  // ENTITY API
  EntityBuilder builder();
  EntityId createEntity();
  EntityId createEntity(std::string_view tag);
  bool isAlive(EntityId id) const;
  void destroyEntity(EntityId id);
  EntityId cloneEntity(EntityId src);

  // ENTITY TAGS API
  void addTag(EntityId id, std::string_view tag);
  void removeTag(EntityId id, std::string_view tag);
  void clearTags(EntityId id);
  bool hasTag(EntityId id, std::string_view tag) const;
  void retagEntity(EntityId id, std::string_view tag);
  const std::unordered_set<EntityId> findWithTag(std::string_view tag) const;
  std::unordered_set<EntityId> findWithTags(std::initializer_list<std::string_view> tags) const;
  const std::unordered_set<TagSymbol>& getTags(EntityId id) const;

  // COMPONENT API
  template <ComponentType T, ComponentPodType P>
  void registerComponent(JSONDeserializer jsonDeserializer, JSONSerializer jsonSerializer, PODDeserializer podDeserializer, PODSerializer podSerializer);

  template <ComponentType T, typename... Args>
  T& addComponent(EntityId id, Args&&... args);

  template <ComponentType T>
  [[nodiscard]]
  T* getComponent(EntityId id) const;

  template <ComponentType T>
  bool hasComponent(EntityId id) const;

  template <ComponentType T>
  void removeComponent(EntityId id);

  template <typename T, typename... Args>
  void registerSystem(Args&&... args);

  template <ComponentType T>
  void assertComponent(EntityId id);

  const ComponentRegistry& getComponentRegistry() const;

  // VALIDATION
  void validate() const;

 private:
  // When an archetype's destroyRow swaps a displaced entity into the freed
  // row, its EntityRecord has to follow. Called by addComponent/removeComponent/
  // destroyEntity after every destroyRow that may have swapped.
  void patchSwappedRecord(std::optional<EntityId> swapped, uint32_t rowSlot);

  EntityManager _entityManager;
  // ECSRegistry must outlive _archetypes: Archetype destructors dereference
  // ComponentInfo* pointers owned by the registry when destructing rows.
  ECSRegistry _registry;
  ArchetypeSet _archetypes;
  std::unordered_map<EntityId, EntityRecord> _entityRecords;
  SystemScheduler _systemScheduler;

  std::unordered_map<TagSymbol, std::unordered_set<EntityId>> _tagToEntities;
  std::unordered_map<EntityId, std::unordered_set<TagSymbol>> _entityToTags;
};

// TEMPLATED METHODS

template <ComponentType... Ts>
View<Ts...> World::view() {
  return View<Ts...>(_archetypes, _registry.getComponentRegistry());
}

template <ComponentType T, ComponentPodType P>
void World::registerComponent(
    JSONDeserializer jsonDeserializer,
    JSONSerializer jsonSerializer,
    PODDeserializer podDeserializer,
    PODSerializer podSerializer) {
  this->_registry.getComponentRegistry().registerComponent<T, P>(
      T::name(),
      std::move(jsonDeserializer),
      std::move(jsonSerializer),
      std::move(podDeserializer),
      std::move(podSerializer));
}

template <ComponentType T, typename... Args>
T& World::addComponent(EntityId id, Args&&... args) {
  if (!_entityManager.isAlive(id)) {
    throw std::runtime_error("Cannot add component to dead entity");
  }
  if (hasComponent<T>(id)) {
    throw std::runtime_error("Component already exists for this entity");
  }

  const auto& reg = _registry.getComponentRegistry();
  const auto* info = reg.getInfo(T::typeId());
  assert(info && "Component not registered");

  EntityRecord& record = _entityRecords[id];
  Archetype* source = record.archetype;
  const uint32_t oldRow = record.row;

  ArchetypeSignature targetSig = source ? source->signature() : ArchetypeSignature{};
  targetSig.bits.set(info->bitIndex);

  Archetype& target = _archetypes.getOrCreate(targetSig, reg);
  const uint32_t newRow = target.allocateRow(id);

  if (source) {
    const ArchetypeSignature sharedSig = source->signature();
    source->moveComponentsTo(target, oldRow, newRow, sharedSig);
    auto swapped = source->destroyRow(oldRow);
    patchSwappedRecord(swapped, oldRow);
  }

  void* slot = target.columnAt(info->bitIndex, newRow);
  info->destruct(slot);
  T* result = new (slot) T(std::forward<Args>(args)...);

  record.archetype = &target;
  record.row = newRow;
  return *result;
}

template <ComponentType T>
[[nodiscard]]
T* World::getComponent(EntityId id) const {
  if (!isAlive(id)) {
    return nullptr;
  }
  auto it = _entityRecords.find(id);
  if (it == _entityRecords.end()) return nullptr;
  const auto& record = it->second;
  if (!record.archetype) return nullptr;

  const auto* info = _registry.getComponentRegistry().getInfo(T::typeId());
  if (!info) return nullptr;
  if (!record.archetype->signature().bits.test(info->bitIndex)) return nullptr;
  return static_cast<T*>(record.archetype->columnAt(info->bitIndex, record.row));
}

template <ComponentType T>
bool World::hasComponent(EntityId id) const {
  if (!isAlive(id)) {
    return false;
  }
  auto it = _entityRecords.find(id);
  if (it == _entityRecords.end()) return false;
  const auto& record = it->second;
  if (!record.archetype) return false;

  const auto* info = _registry.getComponentRegistry().getInfo(T::typeId());
  if (!info) return false;
  return record.archetype->signature().bits.test(info->bitIndex);
}

template <ComponentType T>
void World::removeComponent(EntityId id) {
  if (!isAlive(id)) {
    return;
  }
  if (!hasComponent<T>(id)) {
    return;
  }

  const auto& reg = _registry.getComponentRegistry();
  const auto* info = reg.getInfo(T::typeId());
  assert(info);

  EntityRecord& record = _entityRecords[id];
  Archetype* source = record.archetype;
  const uint32_t oldRow = record.row;

  ArchetypeSignature targetSig = source->signature();
  targetSig.bits.reset(info->bitIndex);

  if (targetSig.bits.none()) {
    auto swapped = source->destroyRow(oldRow);
    patchSwappedRecord(swapped, oldRow);
    record.archetype = nullptr;
    record.row = 0;
    return;
  }

  Archetype& target = _archetypes.getOrCreate(targetSig, reg);
  const uint32_t newRow = target.allocateRow(id);
  source->moveComponentsTo(target, oldRow, newRow, targetSig);
  auto swapped = source->destroyRow(oldRow);
  patchSwappedRecord(swapped, oldRow);

  record.archetype = &target;
  record.row = newRow;
}

template <typename T, typename... Args>
void World::registerSystem(Args&&... args) {
  _systemScheduler.registerSystem<T>(std::forward<Args>(args)...);
}

template <ComponentType T>
void World::assertComponent(EntityId id) {
  if (!hasComponent<T>(id)) {
    throw std::runtime_error(std::string("Missing component: ") + std::string(T::name()));
  }
}

//
//  For the EntityRef API
//
template <typename T>
T* EntityRef::get() const {
  return world->getComponent<T>(id);
}

template <typename T, typename... Args>
T& EntityRef::add(Args&&... args) {
  return world->addComponent<T>(id, std::forward<Args>(args)...);
}

template <typename T>
bool EntityRef::has() const {
  return world->hasComponent<T>(id);
}

template <typename T>
void EntityRef::remove() {
  world->removeComponent<T>(id);
}

//
// For EntityBuilder API
//
template <typename T, typename... Args>
EntityBuilder& EntityBuilder::with(Args&&... args) {
  auto argsTuple = std::make_tuple(std::forward<Args>(args)...);

  _components.emplace_back(
      [this, argsTuple = std::move(argsTuple)]() mutable {
        std::apply([&](auto&&... unpackedArgs) {
          _world.addComponent<T>(_entity, std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
        },
                   std::move(argsTuple));
      });

  return *this;
}

template <typename T, typename Fn>
EntityBuilder& EntityBuilder::with(Fn&& fn)
  requires std::is_invocable_r_v<void, Fn, T&>
{
  auto fnCopy = std::forward<Fn>(fn);

  _components.emplace_back(
      [this, fnCopy = std::move(fnCopy)]() mutable {
        T t{};
        fnCopy(t);
        _world.addComponent<T>(_entity, std::move(t));
      });

  return *this;
}
