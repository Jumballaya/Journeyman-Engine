#include "World.hpp"

#include <stdexcept>

#include "prefab/Prefab.hpp"

namespace {

// Shallow merge for prefab overrides. When both sides are JSON objects,
// top-level keys from `overrides` replace keys in `base` (nested values are not
// deep-merged). When neither is an object, `overrides` wins outright (e.g.,
// overriding a scalar component value). When `base` is an object but
// `overrides` is not, that's almost always a user typo — throw rather than
// silently feeding the deserializer a scalar where it expects fields.
nlohmann::json mergeShallow(const std::string &componentName,
                            const nlohmann::json &base,
                            const nlohmann::json &overrides) {
  if (base.is_object() && !overrides.is_object()) {
    throw std::runtime_error(
        "Prefab override for component '" + componentName +
        "' must be a JSON object to merge with the prefab default.");
  }
  if (!base.is_object() || !overrides.is_object()) {
    return overrides;
  }
  nlohmann::json result = base;
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    result[it.key()] = it.value();
  }
  return result;
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
    if (recIt->second.archetype != nullptr) {
      auto swapped = recIt->second.archetype->destroyRow(recIt->second.row);
      patchSwappedRecord(swapped, recIt->second.row);
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
      if (info && info->jsonDeserialize) {
        info->jsonDeserialize(*this, entity, data);
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
  const auto &reg = _registry.getComponentRegistry();

  try {
    for (const auto &[name, data] : prefab.components) {
      auto maybeId = reg.getComponentIdByName(name);
      if (!maybeId.has_value())
        continue;

      const ComponentInfo *info = reg.getInfo(maybeId.value());
      if (!info || !info->jsonDeserialize)
        continue;

      if (overrides.is_object() && overrides.contains(name)) {
        nlohmann::json merged = mergeShallow(name, data, overrides[name]);
        info->jsonDeserialize(*this, entity, merged);
      } else {
        info->jsonDeserialize(*this, entity, data);
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

const ComponentRegistry &World::getComponentRegistry() const {
  return _registry.getComponentRegistry();
}
