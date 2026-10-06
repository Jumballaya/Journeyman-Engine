#include "Tileset.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

std::optional<bool> Tile::flag(std::string_view name) const {
  auto it = properties.find(name);
  return it != properties.end() && it->is_boolean() ? std::optional(it->get<bool>()) : std::nullopt;
}

std::string resolveTiledPath(const std::string& file, const std::string& relative) {
  if (relative.empty()) return relative;
  const std::filesystem::path base = std::filesystem::path(file).parent_path();
  std::string out = (base / relative).lexically_normal().generic_string();
  if (out.starts_with("./")) out.erase(0, 2);
  return out;
}

nlohmann::json tiledProperties(const nlohmann::json& list) {
  nlohmann::json out = nlohmann::json::object();
  if (!list.is_array()) return out;
  for (const auto& p : list) {
    if (p.is_object() && p.contains("name") && p.contains("value")) out[p["name"].get<std::string>()] = p["value"];
  }
  return out;
}

const Tile* Tileset::frame(uint32_t id, float time) const {
  const Tile* t = tile(id);
  if (!t || t->animation.empty()) return t;
  float total = 0.0f;
  for (const auto& [_, seconds] : t->animation) total += seconds;
  if (total <= 0.0f) return t;
  float at = std::fmod(time, total);
  for (const auto& [frameId, seconds] : t->animation) {
    if (at < seconds) return tile(frameId) ? tile(frameId) : t;
    at -= seconds;
  }
  return t;
}

std::optional<uint32_t> Tileset::find(std::string_view type) const {
  for (uint32_t id = 0; id < _tiles.size(); ++id) {
    if (_tiles[id] && _tiles[id]->type == type) return id;
  }
  return std::nullopt;
}

Tileset Tileset::parse(const nlohmann::json& json, const std::string& path, const ResolveImage& resolve,
                       const std::function<void(const std::string&)>& onMissing) {
  Tileset set;
  if (!json.is_object()) return set;
  set._name = json.value("name", std::string());
  set._tileSize = {json.value("tilewidth", 16.0f), json.value("tileheight", 16.0f)};
  if (const auto offset = json.value("tileoffset", nlohmann::json()); offset.is_object()) {
    set._offset = {offset.value("x", 0.0f), offset.value("y", 0.0f)};
  }
  auto image = [&](const std::string& relative, std::optional<glm::ivec4> rect) {
    const std::string file = resolveTiledPath(path, relative);
    auto found = resolve(file, rect);
    if (!found && onMissing) onMissing(file);
    return found.value_or(TileImage{});
  };

  // One image cut into a grid: every cell is a tile, described or not.
  const int count = std::max(0, json.value("tilecount", 0));
  set._tiles.resize(static_cast<size_t>(count));
  if (const std::string sheet = json.value("image", std::string()); !sheet.empty()) {
    const int columns = std::max(1, json.value("columns", 1));
    const int margin = json.value("margin", 0), spacing = json.value("spacing", 0);
    const glm::ivec2 size(set._tileSize);
    for (int id = 0; id < count; ++id) {
      const glm::ivec2 cell(id % columns, id / columns);
      const glm::ivec2 at = glm::ivec2(margin) + cell * (size + glm::ivec2(spacing));
      set._tiles[static_cast<size_t>(id)] = Tile{.image = image(sheet, glm::ivec4(at, size))};
    }
  }

  for (const auto& spec : json.value("tiles", nlohmann::json::array())) {
    if (!spec.is_object() || !spec.contains("id")) continue;
    const auto id = spec["id"].get<uint32_t>();
    if (id >= set._tiles.size()) set._tiles.resize(id + 1);
    Tile& t = set._tiles[id] ? *set._tiles[id] : set._tiles[id].emplace();
    t.type = spec.value("type", spec.value("class", std::string()));
    t.properties = tiledProperties(spec.value("properties", nlohmann::json()));
    if (const std::string own = spec.value("image", std::string()); !own.empty()) {
      // Tiled 1.9+: a tile may use a sub-rectangle of its image.
      std::optional<glm::ivec4> rect;
      if (spec.contains("width") && spec.contains("height")) {
        rect = glm::ivec4(spec.value("x", 0), spec.value("y", 0), spec["width"].get<int>(), spec["height"].get<int>());
      }
      t.image = image(own, rect);
    }
    for (const auto& f : spec.value("animation", nlohmann::json::array())) {
      t.animation.emplace_back(f.value("tileid", 0u), std::max(1, f.value("duration", 100)) / 1000.0f);
    }
  }
  return set;
}
