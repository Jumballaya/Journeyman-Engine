#include "SceneLoader.hpp"

#include <stdexcept>

#include "../ecs/prefab/Prefab.hpp"
#include "../ecs/prefab/PrefabLoader.hpp"

SceneLoader::SceneLoader(World& world, AssetManager& assetManager) : _world(world), _assetManager(assetManager) {}

std::vector<EntityId> SceneLoader::loadScene(const std::filesystem::path& scenePath) {
  return loadScene(_assetManager.loadAsset(scenePath));
}

std::vector<EntityId> SceneLoader::loadScene(const AssetHandle& handle) {
  const RawAsset& asset = _assetManager.getRawAsset(handle);
  const nlohmann::json scene = nlohmann::json::parse(asset.data.begin(), asset.data.end());
  _groups.clear();
  std::vector<EntityId> created;
  try {
    size_t index = 0;
    for (const auto& entry : scene.value("entities", nlohmann::json::array())) {
      const std::string key = std::to_string(index++);
      if (auto group = entry.value("group", std::string()); !group.empty()) {
        _groups[group].push_back(entry);
      } else if (conditionsHold(entry)) {
        created.push_back(createEntityFromJson(entry, key));
      }
    }
  } catch (...) {
    for (EntityId id : created) _world.destroyEntity(id);
    throw;
  }
  return created;
}

bool SceneLoader::conditionsHold(const nlohmann::json& entityJson) const {
  if (!_condition) return true;
  if (auto key = entityJson.value("if", std::string()); !key.empty() && !_condition(key)) return false;
  if (auto key = entityJson.value("unless", std::string()); !key.empty() && _condition(key)) return false;
  return true;
}

EntityId SceneLoader::createEntityFromJson(const nlohmann::json& entityJson, const std::string& key) {
  constexpr int kMaxNesting = 32;
  if (_nesting >= kMaxNesting) throw std::runtime_error("entities nest too deep: does a prefab hold itself?");
  ++_nesting;
  if (!key.empty()) _keys.push_back(key);
  struct Leave {
    int& n;
    std::vector<std::string>& keys;
    bool keyed;
    ~Leave() {
      --n;
      if (keyed) keys.pop_back();
    }
  } leave{_nesting, _keys, !key.empty()};
  // Inline components are a prefab of their own; a prefab entry ignores any sibling "components".
  Prefab prefab;
  nlohmann::json overrides = nlohmann::json::object();
  if (entityJson.contains("prefab")) {
    const AssetHandle handle = _assetManager.loadAsset(entityJson["prefab"].get<std::string>());
    prefab = PrefabLoader::loadFromBytes(_assetManager.getRawAsset(handle).data);
    overrides = entityJson.value("overrides", overrides);
    if (!overrides.is_object()) overrides = nlohmann::json::object();
  } else {
    prefab.components = PrefabLoader::loadFromJson(entityJson).components;
  }
  if (entityJson.contains("name")) prefab.tags.push_back(entityJson["name"].get<std::string>());
  const nlohmann::json childOverrides = overrides.value("children", nlohmann::json::object());
  overrides.erase("children");
  const EntityId id = _world.instantiatePrefab(prefab, overrides);
  try {
    createChildren(id, prefab.children, childOverrides, key.empty() ? key : key + "/p");
    createChildren(id, entityJson.value("children", nlohmann::json::array()), nlohmann::json::object(),
                   key.empty() ? key : key + "/c");
  } catch (...) {
    _world.destroyEntity(id);  // and the children made so far with it
    throw;
  }
  return id;
}

void SceneLoader::createChildren(EntityId parent, const nlohmann::json& entries, const nlohmann::json& overrides,
                                 const std::string& keyPrefix) {
  if (!entries.is_array()) return;
  size_t index = 0;
  for (nlohmann::json entry : entries) {
    const std::string key = keyPrefix.empty() ? keyPrefix : keyPrefix + std::to_string(index);
    ++index;
    if (!entry.is_object()) continue;
    // An override names the child; it goes where an entry's changes go: its overrides, or its components.
    const auto change = overrides.is_object() ? overrides.find(entry.value("name", std::string())) : overrides.end();
    if (change != overrides.end() && change->is_object()) {
      nlohmann::json& target = entry[entry.contains("prefab") ? "overrides" : "components"];
      if (!target.is_object()) target = nlohmann::json::object();
      target.merge_patch(*change);
    }
    _world.setParent(createEntityFromJson(entry, key), parent, World::Attach::AsAuthored);
  }
}
