#include "World.hpp"

#include <algorithm>
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
nlohmann::json mergeOverride(const std::string &componentName, const nlohmann::json &base,
                             const nlohmann::json &overrides) {
  if (base.is_object() && !overrides.is_object()) {
    throw std::runtime_error("Prefab override for component '" + componentName +
                             "' must be a JSON object to merge with the prefab default.");
  }
  return mergeDeep(base, overrides);
}

} // namespace

EntityRef World::operator[](EntityId id) { return EntityRef{id, this}; }

void World::buildExecutionGraph(TaskGraph &graph, float dt, SystemStage from) {
  _systemScheduler.buildTaskGraph(graph, *this, dt, from);
}

EntityBuilder World::builder() { return EntityBuilder(createEntity(), *this); }

EntityId World::createEntity() {
  EntityId id = _entityManager.create();
  _entityRecords.emplace(id, EntityRecord{});
  return id;
}

EntityId World::createEntity(std::string_view tag) {
  EntityId id = createEntity();
  addTag(id, tag);
  return id;
}

bool World::isAlive(EntityId id) const { return _entityManager.isAlive(id); }

void World::destroyEntity(EntityId id) {
  auto found = _entityRecords.find(id);
  if (found == _entityRecords.end()) return;
  EntityRecord &record = found->second;  // stays valid if hooks create entities

  // Hooks run while every component is still live, and only here (never on
  // migrations). A throwing hook is logged; the others and the destroy still run.
  _components.forEachRegisteredComponent([&](ComponentId cid) {
    const ComponentInfo *info = _components.getInfo(cid);
    void *component = info->onDestroy ? componentData(id, info) : nullptr;
    if (!component) return;
    try {
      info->onDestroy(component);
    } catch (const std::exception &e) {
      JM_LOG_ERROR("[World] onDestroy hook for component '{}' threw: {}", info->name, e.what());
    } catch (...) {
      JM_LOG_ERROR("[World] onDestroy hook for component '{}' threw unknown", info->name);
    }
  });
  if (record.archetype) destroyRow(*record.archetype, record.row);
  _entityRecords.erase(id);

  if (auto tags = _entityToTags.find(id); tags != _entityToTags.end()) {
    for (const std::string &tag : tags->second) untag(id, tag);
    _entityToTags.erase(tags);
  }
  _entityManager.destroy(id);
}

EntityId World::instantiatePrefab(const Prefab &prefab, const nlohmann::json &overrides) {
  EntityId entity = createEntity();
  instantiatePrefabInto(entity, prefab, overrides);
  return entity;
}

void World::instantiatePrefabInto(EntityId entity, const Prefab &prefab, const nlohmann::json &overrides) {
  // Unknown components are skipped, overrides and all.
  auto add = [&](const std::string &name, const nlohmann::json &data, const nlohmann::json *override) {
    const ComponentInfo *info = _components.getInfoByName(name);
    if (!info || !info->addFromJson) return;
    info->addFromJson(*this, entity, override ? mergeOverride(name, data, *override) : data);
  };

  try {
    for (const auto &[name, data] : prefab.components) {
      auto override = overrides.find(name);
      add(name, data, override != overrides.end() ? &*override : nullptr);
    }
    // Overrides may also add components the prefab doesn't have.
    if (overrides.is_object()) {
      for (const auto &[name, data] : overrides.items()) {
        const bool inPrefab = std::any_of(prefab.components.begin(), prefab.components.end(),
                                          [&](const auto &c) { return c.first == name; });
        if (!inPrefab) add(name, data, nullptr);
      }
    }
    for (const auto &tag : prefab.tags) addTag(entity, tag);
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

void *World::componentData(EntityId id, const ComponentInfo *info) const {
  auto it = _entityRecords.find(id);
  if (!info || it == _entityRecords.end()) return nullptr;
  Archetype *archetype = it->second.archetype;
  if (!archetype || !archetype->signature().bits.test(info->bitIndex)) return nullptr;
  return archetype->columnAt(info->bitIndex, it->second.row);
}

World::EntityRecord &World::migrate(EntityId id, size_t bitIndex, bool present) {
  EntityRecord &record = _entityRecords.at(id);
  ArchetypeSignature signature = record.archetype ? record.archetype->signature() : ArchetypeSignature{};
  signature.bits.set(bitIndex, present);

  Archetype *target = signature.bits.none() ? nullptr : &_archetypes.getOrCreate(signature, _components);
  const uint32_t row = target ? target->allocateRow(id) : 0;
  if (record.archetype) {
    if (target) record.archetype->moveComponentsTo(*target, record.row, row, signature);
    destroyRow(*record.archetype, record.row);
  }
  record = EntityRecord{target, row};
  return record;
}

void World::destroyRow(Archetype &archetype, uint32_t row) {
  if (auto moved = archetype.destroyRow(row)) _entityRecords.at(*moved).row = row;
}

void World::addTag(EntityId id, std::string_view tag) {
  if (!isAlive(id)) return;
  _entityToTags[id].emplace(tag);
  _tagToEntities[std::string(tag)].insert(id);
}

void World::removeTag(EntityId id, std::string_view tag) {
  auto tags = _entityToTags.find(id);
  if (tags == _entityToTags.end()) return;
  auto it = tags->second.find(tag);
  if (it == tags->second.end()) return;
  tags->second.erase(it);
  if (tags->second.empty()) _entityToTags.erase(tags);
  untag(id, tag);
}

void World::untag(EntityId id, std::string_view tag) {
  auto it = _tagToEntities.find(tag);
  if (it == _tagToEntities.end()) return;
  it->second.erase(id);
  if (it->second.empty()) _tagToEntities.erase(it);
}

bool World::hasTag(EntityId id, std::string_view tag) const {
  auto it = _entityToTags.find(id);
  return it != _entityToTags.end() && it->second.contains(tag);
}

const std::unordered_set<EntityId> World::findWithTag(std::string_view tag) const {
  auto it = _tagToEntities.find(tag);
  return it != _tagToEntities.end() ? it->second : std::unordered_set<EntityId>{};
}

std::vector<std::string> World::tagNames(EntityId id) const {
  auto it = _entityToTags.find(id);
  if (it == _entityToTags.end()) return {};
  return {it->second.begin(), it->second.end()};
}

std::vector<EntityId> World::entities() const {
  std::vector<EntityId> out;
  out.reserve(_entityRecords.size());
  for (const auto &[id, _] : _entityRecords) out.push_back(id);
  return out;
}

std::vector<std::string> World::componentNames(EntityId id) const {
  std::vector<std::string> out;
  _components.forEachRegisteredComponent([&](ComponentId cid) {
    const ComponentInfo *info = _components.getInfo(cid);
    if (componentData(id, info)) out.push_back(info->name);
  });
  return out;
}

std::optional<World::ScriptFieldRef> World::findScriptField(std::string_view component, std::string_view field) const {
  const ComponentInfo *info = _components.getInfoByName(component);
  if (!info) return std::nullopt;
  for (uint32_t i = 0; i < info->scriptFields.size(); ++i) {
    if (info->scriptFields[i].name == field) return ScriptFieldRef{info, i};
  }
  return std::nullopt;
}

std::optional<uint32_t> World::readScriptField(EntityId id, ScriptFieldRef field) const {
  void *component = componentData(id, field.component);
  if (!component) return std::nullopt;
  uint32_t bits;
  std::memcpy(&bits, field.component->scriptFields[field.index].locate(component), 4);
  return bits;
}

bool World::writeScriptField(EntityId id, ScriptFieldRef field, uint32_t bits) {
  void *component = componentData(id, field.component);
  if (!component) return false;
  std::memcpy(field.component->scriptFields[field.index].locate(component), &bits, 4);
  return true;
}

bool World::hasComponentNamed(EntityId id, std::string_view component) const {
  return componentData(id, _components.getInfoByName(component)) != nullptr;
}
