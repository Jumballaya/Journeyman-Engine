#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "View.hpp"
#include "archetype/Archetype.hpp"
#include "archetype/ArchetypeSet.hpp"
#include "archetype/ArchetypeSignature.hpp"
#include "component/ComponentConcepts.hpp"
#include "component/ComponentInfo.hpp"
#include "component/ComponentRegistry.hpp"
#include "component/ComponentSpec.hpp"
#include "entity/EntityBuilder.hpp"
#include "entity/EntityId.hpp"
#include "entity/EntityManager.hpp"
#include "entity/EntityRef.hpp"
#include "system/SystemScheduler.hpp"

struct Prefab;

class World {
public:
  World() = default;
  World(const World &) = delete;
  World &operator=(const World &) = delete;

  EntityRef operator[](EntityId id);

  // Updates the systems in order on this thread; stages before `from` are
  // skipped (an edit preview only renders).
  void runSystems(float dt, SystemStage from = SystemStage::Input);

  template <ComponentType... Ts> View<Ts...> view() { return View<Ts...>(_archetypes, _components); }

  // ENTITY API
  EntityBuilder builder();
  EntityId createEntity();
  EntityId createEntity(std::string_view tag);
  bool isAlive(EntityId id) const;
  void destroyEntity(EntityId id);

  // Safe while systems iterate: the entity lives until the frame loop drains
  // takePendingDestroys(); isPendingDestroy lets systems skip it meanwhile.
  void destroyDeferred(EntityId id);
  bool isPendingDestroy(EntityId id) const;
  std::vector<EntityId> takePendingDestroys();

  // Overrides deep-merge into the prefab's components; ones it lacks are added.
  // Atomic: if a component's fromJson throws, the entity is destroyed first.
  EntityId instantiatePrefab(const Prefab &prefab, const nlohmann::json &overrides = nlohmann::json());
  // Same as above, but onto an already-created (component-less) entity —
  // used when the id had to be handed out before instantiation.
  void instantiatePrefabInto(EntityId entity, const Prefab &prefab, const nlohmann::json &overrides);

  // ENTITY HIERARCHY
  // A child is destroyed with its parent. What else being a child means (its
  // transform following the parent's) belongs to modules, which hear about
  // each change. How the child's existing state relates to the new parent:
  enum class Attach {
    AsAuthored,  // it was authored relative to the parent (scene and prefab children)
    InPlace,     // it stays as it is, wherever the parent is (attaching at run time)
  };
  // Main thread, between frames. kNoEntityId detaches. False (nothing changes) if
  // either is dead or the parent is the child or one of its descendants.
  bool setParent(EntityId child, EntityId parent, Attach how = Attach::InPlace);
  EntityId parentOf(EntityId id) const;  // kNoEntityId if none
  const std::vector<EntityId> &childrenOf(EntityId id) const;  // in the order attached
  using ParentListener = std::function<void(EntityId child, EntityId parent, Attach how)>;
  void onParentChanged(ParentListener listener) { _parentListeners.push_back(std::move(listener)); }

  // ENTITY TAGS API
  void addTag(EntityId id, std::string_view tag);
  void removeTag(EntityId id, std::string_view tag);
  bool hasTag(EntityId id, std::string_view tag) const;
  const std::unordered_set<EntityId> findWithTag(std::string_view tag) const;

  // COMPONENT API
  template <ComponentType T>
  void registerComponent(ComponentSpec<T> spec = {});

  // Script access to fields declared in ComponentSpec::scriptFields, as raw
  // 4-byte values. Reads/writes on a dead entity or missing component fail.
  struct ScriptFieldRef {
    const ComponentInfo *component;
    uint32_t index;
  };
  std::optional<ScriptFieldRef> findScriptField(std::string_view component, std::string_view field) const;
  std::optional<uint32_t> readScriptField(EntityId id, ScriptFieldRef field) const;
  bool writeScriptField(EntityId id, ScriptFieldRef field, uint32_t bits);
  bool hasComponentNamed(EntityId id, std::string_view component) const;

  // For tools (an editor's view of a running game): every live entity, and
  // the names of the components and tags one has (tags sorted).
  std::vector<EntityId> entities() const;
  std::vector<std::string> componentNames(EntityId id) const;
  std::vector<std::string> tagNames(EntityId id) const;

  // Throws if the entity is dead or already has a T.
  template <ComponentType T, typename... Args>
  T &addComponent(EntityId id, Args &&...args);

  template <ComponentType T>
  [[nodiscard]]
  T *getComponent(EntityId id) const {
    return static_cast<T *>(componentData(id, _components.getInfo(T::typeId())));
  }

  template <ComponentType T> bool hasComponent(EntityId id) const { return getComponent<T>(id) != nullptr; }

  template <ComponentType T> void removeComponent(EntityId id) {
    const ComponentInfo *info = _components.getInfo(T::typeId());
    if (componentData(id, info)) migrate(id, info->bitIndex, false);
  }

  template <typename T, typename... Args> void registerSystem(Args &&...args) {
    _systemScheduler.registerSystem<T>(std::forward<Args>(args)...);
  }

  const ComponentRegistry &getComponentRegistry() const { return _components; }

private:
  struct EntityRecord {
    Archetype *archetype = nullptr;  // null while the entity has no components
    uint32_t row = 0;
  };

  // The entity's `info` component, or null if it (or the entity) is absent.
  void *componentData(EntityId id, const ComponentInfo *info) const;
  // Moves the entity to the archetype with `bitIndex` added or removed.
  EntityRecord &migrate(EntityId id, size_t bitIndex, bool present);
  void destroyRow(Archetype &archetype, uint32_t row);
  void untag(EntityId id, std::string_view tag);

  EntityManager _entityManager;
  // Must outlive _archetypes: their destructors use its ComponentInfos.
  ComponentRegistry _components;
  ArchetypeSet _archetypes;
  std::unordered_map<EntityId, EntityRecord> _entityRecords;  // one per live entity
  SystemScheduler _systemScheduler;

  std::unordered_set<EntityId> _pendingDestroy;
  std::vector<EntityId> _pendingOrder;

  std::unordered_map<EntityId, EntityId> _parents;
  std::unordered_map<EntityId, std::vector<EntityId>> _children;
  std::vector<ParentListener> _parentListeners;
  void unlinkFromParent(EntityId child);

  std::map<std::string, std::unordered_set<EntityId>, std::less<>> _tagToEntities;
  std::unordered_map<EntityId, std::set<std::string, std::less<>>> _entityToTags;
};

// TEMPLATED METHODS

template <ComponentType T>
void World::registerComponent(ComponentSpec<T> spec) {
  ComponentInfo info;
  info.addFromJson = [fromJson = std::move(spec.fromJson)](World &world, EntityId id, const nlohmann::json &json) {
    T component{};
    if (fromJson) fromJson(component, json, id);
    world.addComponent<T>(id, std::move(component));
  };
  info.scriptFields = std::move(spec.scriptFields);
  info.schema = std::move(spec.schema);
  if (spec.onDestroy) {
    info.onDestroy = [onDestroy = std::move(spec.onDestroy)](void *c) { onDestroy(*static_cast<T *>(c)); };
  }
  _components.registerComponent<T>(std::move(info));
}

template <ComponentType T, typename... Args>
T &World::addComponent(EntityId id, Args &&...args) {
  const ComponentInfo *info = _components.getInfo(T::typeId());
  assert(info && "Component not registered");
  if (!isAlive(id)) throw std::runtime_error("Cannot add component to dead entity");
  if (componentData(id, info)) throw std::runtime_error("Component already exists for this entity");

  // Built before any row moves: args may refer to components stored in them.
  T component(std::forward<Args>(args)...);
  const EntityRecord &record = migrate(id, info->bitIndex, true);
  void *slot = record.archetype->columnAt(info->bitIndex, record.row);
  info->destruct(slot);
  return *new (slot) T(std::move(component));
}

// ---- EntityRef ----
template <typename T> T *EntityRef::get() const { return world->getComponent<T>(id); }

template <typename T, typename... Args> T &EntityRef::add(Args &&...args) {
  return world->addComponent<T>(id, std::forward<Args>(args)...);
}

template <typename T> bool EntityRef::has() const { return world->hasComponent<T>(id); }

template <typename T> void EntityRef::remove() { world->removeComponent<T>(id); }

// ---- EntityBuilder ----
template <typename T, typename... Args>
EntityBuilder &EntityBuilder::with(Args &&...args) {
  _components.emplace_back([&world = _world, entity = _entity, ... args = std::forward<Args>(args)]() mutable {
    world.addComponent<T>(entity, std::move(args)...);
  });
  return *this;
}

template <typename T, typename Fn>
EntityBuilder &EntityBuilder::with(Fn &&fn)
  requires std::is_invocable_r_v<void, Fn, T &>
{
  _components.emplace_back([&world = _world, entity = _entity, fn = std::forward<Fn>(fn)]() mutable {
    T component{};
    fn(component);
    world.addComponent<T>(entity, std::move(component));
  });
  return *this;
}
