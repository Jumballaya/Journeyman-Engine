#include "Entities.hpp"

#include <map>

#include "Icons.hpp"

Json mergeDeep(const Json& base, const Json& overrides) {
  if (!base.is_object() || !overrides.is_object()) return overrides;
  Json result = base;
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    result[it.key()] = result.contains(it.key()) ? mergeDeep(result[it.key()], it.value()) : it.value();
  }
  return result;
}

const Json* prefabJson(const Project& project, const std::string& path) {
  struct Cached {
    std::filesystem::file_time_type modified;
    Json json;
  };
  static std::map<std::string, Cached> cache;
  std::error_code ec;
  const auto modified = std::filesystem::last_write_time(project.abs(path), ec);
  if (ec) return nullptr;
  const std::string key = project.abs(path).string();
  auto it = cache.find(key);
  if (it == cache.end() || it->second.modified != modified) {
    Json json = Json::parse(project.readText(path), nullptr, false);
    if (json.is_discarded() || !json.is_object()) return nullptr;
    it = cache.insert_or_assign(key, Cached{modified, std::move(json)}).first;
  }
  return &it->second.json;
}

Json effectiveComponents(const Project& project, const Json& entity) {
  if (!entity.contains("prefab")) return entity.value("components", Json::object());
  const Json* prefab = prefabJson(project, entity.value("prefab", std::string()));
  Json components = prefab ? prefab->value("components", Json::object()) : Json::object();
  const Json overrides = entity.value("overrides", Json::object());
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    if (it.key() == "tags") continue;
    components[it.key()] = components.contains(it.key()) ? mergeDeep(components[it.key()], it.value()) : it.value();
  }
  return components;
}

bool fromPrefab(const Project& project, const Json& entity, const std::string& component) {
  if (!entity.contains("prefab")) return false;
  const Json* prefab = prefabJson(project, entity.value("prefab", std::string()));
  return prefab && prefab->contains("components") && (*prefab)["components"].contains(component);
}

bool overrides(const Json& entity, const std::string& component, const std::string& key) {
  auto o = entity.find("overrides");
  if (o == entity.end() || !o->contains(component)) return false;
  const Json& c = (*o)[component];
  return c.is_object() && c.contains(key);
}

Json& editableComponent(Json& entity, const std::string& component) {
  Json& holder = entity.contains("prefab") ? entity["overrides"] : entity["components"];
  if (!holder.is_object()) holder = Json::object();
  Json& c = holder[component];
  if (!c.is_object()) c = Json::object();
  return c;
}

Json fieldValue(const Project& project, const Json& entity, const std::string& component, const std::string& key) {
  const Json components = effectiveComponents(project, entity);
  auto c = components.find(component);
  if (c == components.end() || !c->is_object()) return nullptr;
  auto v = c->find(key);
  return v == c->end() ? Json(nullptr) : *v;
}

namespace {
std::map<std::string, ComponentSchema>& schemas() {
  static std::map<std::string, ComponentSchema> all;
  return all;
}
}  // namespace

void setComponentSchemas(std::map<std::string, ComponentSchema> all) { schemas() = std::move(all); }

const std::map<std::string, ComponentSchema>& componentSchemas() { return schemas(); }

const ComponentSchema* componentSchema(const std::string& name) {
  auto it = schemas().find(name);
  return it == schemas().end() ? nullptr : &it->second;
}

std::string componentLabel(const std::string& name) {
  if (const ComponentSchema* schema = componentSchema(name); schema && !schema->label.empty()) return schema->label;
  return name.ends_with("Component") ? name.substr(0, name.size() - 9) : name;
}

const char* componentIcon(const std::string& name) {
  static const std::map<std::string, const char*> icons = {
      {"TransformComponent", ICON_ARROWS_OUT_CARDINAL}, {"SpriteComponent", ICON_IMAGE},
      {"SpriteAnimationComponent", ICON_FILM_STRIP},     {"VelocityComponent", ICON_WIND},
      {"BoxColliderComponent", ICON_BOUNDING_BOX},       {"LifetimeComponent", ICON_HOURGLASS},
      {"ScrollWrapComponent", ICON_ARROWS_DOWN_UP},      {"ScriptComponent", ICON_CODE},
      {"UIDocumentComponent", ICON_BROWSER},             {"TextComponent", ICON_TEXT_T},
      {"AudioEmitterComponent", ICON_SPEAKER_HIGH},      {"TileMapComponent", ICON_GRID_FOUR},
  };
  auto it = icons.find(name);
  return it == icons.end() ? ICON_PUZZLE_PIECE : it->second;
}

const char* entityIcon(const Json& components) {
  for (const char* telling : {"TileMapComponent", "UIDocumentComponent", "TextComponent", "SpriteAnimationComponent",
                              "SpriteComponent", "AudioEmitterComponent", "BoxColliderComponent", "ScriptComponent"}) {
    if (components.contains(telling)) return componentIcon(telling);
  }
  return ICON_CUBE_TRANSPARENT;
}
