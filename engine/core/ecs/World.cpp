#include "World.hpp"

#include <cstring>
#include <stdexcept>

#include "../logger/LogMacros.hpp"
#include "prefab/Prefab.hpp"

namespace {

// Objects merge key by key (overriding params.pattern keeps other params);
// arrays and scalars are replaced.
nlohmann::json mergeDeep(const nlohmann::json &base, const nlohmann::json &overrides) {
  if (!base.is_object() || !overrides.is_object()) {
    return overrides;
  }
  nlohmann::json result = base;
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    result[it.key()] = result.contains(it.key()) ? mergeDeep(result[it.key()], it.value()) : it.value();
  }
  return result;
}

// Throws when a non-object overrides an object: almost always a typo that
// would otherwise feed fromJson a scalar.
nlohmann::json mergeOverride(const std::string &componentName,
                            const nlohmann::json &base,
                            const nlohmann::json &overrides) {
  if (base.is_object() && !overrides.is_object()) {
    throw std::runtime_error(
        "Prefab override for component '" + componentName +
        "' must be a JSON object to merge with the prefab default.");
  }
  return mergeDeep(base, overrides);
}

} // namespace

EntityRef World::operator[](EntityId id) { return EntityRef{id, this}; }

void World::buildExecutionGraph(TaskGraph &graph, float dt) {
  _systemScheduler.buildTaskGraph(graph, *this, dt);
}

EntityBuilder World::builder() {
  EntityId id = createEntity();
  return EntityBuilder(id, *this);
}

EntityId World::createEntity() { return _entityManager.create(); }

EntityId World::createEntity(std::string_view tag) {
  EntityId id = createEntity();
  addTag(id, tag);
  return id;
}

bool World::isAlive(EntityId id) const { return _entityManager.isAlive(id); }

void World::destroyEntity(EntityId id) {
  if (!isAlive(id))
    return;

  auto recIt = _entityRecords.find(id);
  if (recIt != _entityRecords.end()) {
    Archetype *archetype = recIt->second.archetype;
    const uint32_t row = recIt->second.row;
    if (archetype != nullptr) {
      // Hooks run before destroyRow (components still live), here rather than
      // in destroyRow so migrations don't fire them. Order between hooks: unspecified.
      const auto &reg = _registry.getComponentRegistry();
      reg.forEachRegisteredComponent([&](ComponentId cid) {
        const ComponentInfo *info = reg.getInfo(cid);
        if (!info || !info->onDestroy) return;
        if (!archetype->signature().bits.test(info->bitIndex)) return;
        void *componentPtr = archetype->columnAt(info->bitIndex, row);
        // A throwing hook is logged; the other hooks and the destroy still run.
        try {
          info->onDestroy(componentPtr);
        } catch (const std::exception &e) {
          JM_LOG_ERROR("[World] onDestroy hook for component '{}' threw: {}",
                       info->name, e.what());
        } catch (...) {
          JM_LOG_ERROR(
              "[World] onDestroy hook for component '{}' threw unknown",
              info->name);
        }
      });

      auto swapped = archetype->destroyRow(row);
      patchSwappedRecord(swapped, row);
    }
    _entityRecords.erase(recIt);
  }

  auto it = _entityToTags.find(id);
  if (it != _entityToTags.end()) {
    for (TagSymbol tag : it->second) {
      _tagToEntities[tag].erase(id);
    }
    _entityToTags.erase(it);
  }

  _entityManager.destroy(id);
}

EntityId World::cloneEntity(EntityId src) {
  if (!isAlive(src))
    return EntityId{};
  EntityId dst = createEntity();

  const auto &tags = getTags(src);
  for (TagSymbol tag : tags) {
    _tagToEntities[tag].insert(dst);
    _entityToTags[dst].insert(tag);
  }

  auto srcIt = _entityRecords.find(src);
  if (srcIt == _entityRecords.end() || srcIt->second.archetype == nullptr) {
    return dst;
  }

  Archetype *srcArchetype = srcIt->second.archetype;
  const uint32_t srcRow = srcIt->second.row;
  const auto &reg = _registry.getComponentRegistry();

  Archetype &dstArchetype =
      _archetypes.getOrCreate(srcArchetype->signature(), reg);
  const uint32_t dstRow = dstArchetype.allocateRow(dst);

  reg.forEachRegisteredComponent([&](ComponentId cid) {
    const auto *info = reg.getInfo(cid);
    if (!info)
      return;
    if (!srcArchetype->signature().bits.test(info->bitIndex))
      return;
    void *dstSlot = dstArchetype.columnAt(info->bitIndex, dstRow);
    const void *srcSlot = srcArchetype->columnAt(info->bitIndex, srcRow);
    info->destruct(dstSlot);
    info->copyConstruct(dstSlot, srcSlot);
  });

  _entityRecords[dst] = EntityRecord{&dstArchetype, dstRow};
  return dst;
}

EntityId World::instantiatePrefab(const Prefab &prefab) {
  EntityId entity = createEntity();
  const auto &reg = _registry.getComponentRegistry();

  try {
    for (const auto &[name, data] : prefab.components) {
      auto maybeId = reg.getComponentIdByName(name);
      if (!maybeId.has_value())
        continue;

      const ComponentInfo *info = reg.getInfo(maybeId.value());
      if (info && info->addFromJson) {
        info->addFromJson(*this, entity, data);
      }
    }

    for (const auto &tag : prefab.tags) {
      addTag(entity, tag);
    }
  } catch (...) {
    destroyEntity(entity);
    throw;
  }

  return entity;
}

EntityId World::instantiatePrefab(const Prefab &prefab,
                                  const nlohmann::json &overrides) {
  EntityId entity = createEntity();
  instantiatePrefabInto(entity, prefab, overrides);
  return entity;
}

void World::instantiatePrefabInto(EntityId entity, const Prefab &prefab,
                                  const nlohmann::json &overrides) {
  const auto &reg = _registry.getComponentRegistry();

  try {
    for (const auto &[name, data] : prefab.components) {
      auto maybeId = reg.getComponentIdByName(name);
      if (!maybeId.has_value())
        continue;

      const ComponentInfo *info = reg.getInfo(maybeId.value());
      if (!info || !info->addFromJson)
        continue;

      if (overrides.is_object() && overrides.contains(name)) {
        nlohmann::json merged = mergeOverride(name, data, overrides[name]);
        info->addFromJson(*this, entity, merged);
      } else {
        info->addFromJson(*this, entity, data);
      }
    }

    for (const auto &tag : prefab.tags) {
      addTag(entity, tag);
    }
  } catch (...) {
    destroyEntity(entity);
    throw;
  }
}

void World::destroyDeferred(EntityId id) {
  std::lock_guard lock(_pendingMutex);
  if (_pendingDestroy.insert(id).second) {
    _pendingOrder.push_back(id);
  }
}

bool World::isPendingDestroy(EntityId id) const {
  std::lock_guard lock(_pendingMutex);
  return _pendingDestroy.contains(id);
}

std::vector<EntityId> World::takePendingDestroys() {
  std::lock_guard lock(_pendingMutex);
  _pendingDestroy.clear();
  return std::exchange(_pendingOrder, {});
}

void World::patchSwappedRecord(std::optional<EntityId> swapped,
                               uint32_t rowSlot) {
  if (!swapped)
    return;
  auto it = _entityRecords.find(*swapped);
  if (it == _entityRecords.end())
    return;
  it->second.row = rowSlot;
}

void World::addTag(EntityId id, std::string_view tag) {
  if (!isAlive(id))
    return;

  TagSymbol symbol = toTagSymbol(tag);
  _tagToEntities[symbol].insert(id);
  _entityToTags[id].insert(symbol);
}

void World::removeTag(EntityId id, std::string_view tag) {
  if (!isAlive(id))
    return;
  TagSymbol symbol = toTagSymbol(tag);
  _tagToEntities[symbol].erase(id);
  _entityToTags[id].erase(symbol);

  if (_tagToEntities[symbol].empty()) {
    _tagToEntities.erase(symbol);
  }
  if (_entityToTags[id].empty()) {
    _entityToTags.erase(id);
  }
}

void World::clearTags(EntityId id) {
  if (!isAlive(id))
    return;
  auto it = _entityToTags.find(id);
  if (it == _entityToTags.end())
    return;

  for (TagSymbol tag : it->second) {
    _tagToEntities[tag].erase(id);
  }

  _entityToTags.erase(it);
}

bool World::hasTag(EntityId id, std::string_view tag) const {
  if (!isAlive(id))
    return false;
  TagSymbol symbol = toTagSymbol(tag);
  auto it = _entityToTags.find(id);
  if (it == _entityToTags.end())
    return false;
  return it->second.contains(symbol);
}

void World::retagEntity(EntityId id, std::string_view tag) {
  clearTags(id);
  addTag(id, tag);
}

const std::unordered_set<EntityId>
World::findWithTag(std::string_view tag) const {
  static const std::unordered_set<EntityId> empty;
  TagSymbol symbol = toTagSymbol(tag);
  auto it = _tagToEntities.find(symbol);
  return it != _tagToEntities.end() ? it->second : empty;
}

std::unordered_set<EntityId>
World::findWithTags(std::initializer_list<std::string_view> tags) const {
  std::vector<const std::unordered_set<EntityId> *> sets;
  for (auto tag : tags) {
    TagSymbol symbol = toTagSymbol(tag);
    auto it = _tagToEntities.find(symbol);
    if (it == _tagToEntities.end())
      return {};
    sets.push_back(&it->second);
  }

  if (sets.empty())
    return {};

  // get the shortest list up front
  std::sort(sets.begin(), sets.end(),
            [](const std::unordered_set<EntityId> *a,
               const std::unordered_set<EntityId> *b) {
              return a->size() < b->size();
            });

  // build the intersection of all of the tags' entities
  std::unordered_set<EntityId> result = *sets[0];
  for (size_t i = 1; i < sets.size(); ++i) {
    std::unordered_set<EntityId> temp;
    for (EntityId id : *sets[i]) {
      if (result.contains(id)) {
        temp.insert(id);
      }
    }
    result = std::move(temp);
    if (result.empty())
      break;
  }

  return result;
}

const std::unordered_set<TagSymbol> &World::getTags(EntityId id) const {
  static const std::unordered_set<TagSymbol> empty;
  auto it = _entityToTags.find(id);
  return it != _entityToTags.end() ? it->second : empty;
}

void World::validate() const {
  for (const auto &[id, tags] : _entityToTags) {
    if (!isAlive(id)) {
      throw std::runtime_error("Entity has tags but is not alive.");
    }
  }
}

namespace {
void *componentIn(Archetype *archetype, uint32_t row, const ComponentInfo &info) {
  if (!archetype || !archetype->signature().bits.test(info.bitIndex)) return nullptr;
  return archetype->columnAt(info.bitIndex, row);
}
}  // namespace

std::optional<World::ScriptFieldRef> World::findScriptField(std::string_view component, std::string_view field) const {
  const auto &reg = getComponentRegistry();
  auto id = reg.getComponentIdByName(component);
  const ComponentInfo *info = id ? reg.getInfo(*id) : nullptr;
  if (!info) return std::nullopt;
  for (uint32_t i = 0; i < info->scriptFields.size(); ++i) {
    if (info->scriptFields[i].name == field) return ScriptFieldRef{info, i};
  }
  return std::nullopt;
}

std::optional<uint32_t> World::readScriptField(EntityId id, ScriptFieldRef field) const {
  auto it = _entityRecords.find(id);
  if (!isAlive(id) || it == _entityRecords.end()) return std::nullopt;
  void *component = componentIn(it->second.archetype, it->second.row, *field.component);
  if (!component) return std::nullopt;
  uint32_t bits;
  std::memcpy(&bits, field.component->scriptFields[field.index].locate(component), 4);
  return bits;
}

bool World::writeScriptField(EntityId id, ScriptFieldRef field, uint32_t bits) {
  auto it = _entityRecords.find(id);
  if (!isAlive(id) || it == _entityRecords.end()) return false;
  void *component = componentIn(it->second.archetype, it->second.row, *field.component);
  if (!component) return false;
  std::memcpy(field.component->scriptFields[field.index].locate(component), &bits, 4);
  return true;
}

bool World::hasComponentNamed(EntityId id, std::string_view component) const {
  const auto &reg = getComponentRegistry();
  auto cid = reg.getComponentIdByName(component);
  const ComponentInfo *info = cid ? reg.getInfo(*cid) : nullptr;
  auto it = _entityRecords.find(id);
  return info && isAlive(id) && it != _entityRecords.end() && componentIn(it->second.archetype, it->second.row, *info);
}

const ComponentRegistry &World::getComponentRegistry() const {
  return _registry.getComponentRegistry();
}
