#include "Entities.hpp"

#include <cmath>
#include <cstring>
#include <map>

#include "Icons.hpp"

void wholeNumbersAsIntegers(Json& value) {
  if (value.is_number_float()) {
    // Values that passed through a float (0.4f is 0.4000000059604645) read as typed.
    double v = value.get<double>();
    if (std::abs(v) < 1e9) v = std::round(v * 1e6) / 1e6;
    if (std::abs(v) < 1e15 && v == std::floor(v)) value = static_cast<int64_t>(v);
    else value = v;
  } else if (value.is_structured()) {
    for (auto& child : value) wholeNumbersAsIntegers(child);
  }
}

std::string prefabImage(const Project& project, const std::string& path) {
  const Json* prefab = prefabJson(project, path);
  if (!prefab) return {};
  const Json components = prefab->value("components", Json::object());
  const std::string texture = components.value("SpriteComponent", Json::object()).value("texture", std::string());
  if (!texture.empty()) return texture;
  const Json anim = components.value("SpriteAnimationComponent", Json::object());
  const std::string atlas = anim.value("atlasPath", std::string());
  const Json animations = anim.value("animations", Json::object());
  const std::string current = anim.value("current", std::string());
  const Json chosen = animations.contains(current) ? animations[current] : animations.empty() ? Json() : animations.begin().value();
  const Json regions = chosen.is_object() ? chosen.value("regions", Json()) : Json();
  if (atlas.empty() || !regions.is_array() || regions.empty() || !regions[0].is_string()) return {};
  return atlas + "#" + regions[0].get<std::string>();
}

std::string assetImage(const Project& project, const std::string& path) {
  switch (assetKindOf(path)) {
    case AssetKind::Prefab: return prefabImage(project, path);
    case AssetKind::Tileset: {
      // The first tile that names a plain region (variants at 0: the full, inner one).
      const Json tileset = Json::parse(project.readText(path), nullptr, false);
      if (tileset.is_discarded()) return {};
      const std::string atlas = tileset.value("atlas", std::string());
      for (const auto& [_, tile] : tileset.value("tiles", Json::object()).items()) {
        const Json image = tile.value("image", Json());
        std::string name = image.is_array() && !image.empty() && image[0].is_string() ? image[0].get<std::string>()
                           : image.is_string()                                       ? image.get<std::string>()
                                                                                     : "";
        for (const char* var : {"{mask}", "{frame}"}) {
          if (const size_t at = name.find(var); at != std::string::npos) name.replace(at, std::strlen(var), "0");
        }
        if (!name.empty() && name.find('{') == std::string::npos) return atlas + "#" + name;
      }
      return {};
    }
    default: return {};
  }
}

std::vector<std::pair<std::string, std::vector<std::string>>> overridesOf(const Json& entity) {
  std::vector<std::pair<std::string, std::vector<std::string>>> out;
  for (const auto& [component, fields] : entity.value("overrides", Json::object()).items()) {
    if (component == "tags") continue;
    std::vector<std::string> keys;
    if (fields.is_object()) {
      for (const auto& [key, _] : fields.items()) keys.push_back(key);
    }
    out.emplace_back(component, keys);
  }
  return out;
}

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
  Json changes = entity.value("overrides", Json::object());
  if (!changes.is_object()) changes = Json::object();
  changes.erase("tags");
  return mergeDeep(prefab ? prefab->value("components", Json::object()) : Json::object(), changes);
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
std::map<std::string, ComponentSchema> gSchemas;
}  // namespace

void setComponentSchemas(std::map<std::string, ComponentSchema> all) { gSchemas = std::move(all); }

const std::map<std::string, ComponentSchema>& componentSchemas() { return gSchemas; }

const ComponentSchema* componentSchema(const std::string& name) {
  auto it = gSchemas.find(name);
  return it == gSchemas.end() ? nullptr : &it->second;
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
