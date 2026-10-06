#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "../renderer2d/TextureHandle.hpp"

struct TileImage {
  TextureHandle texture;
  glm::vec4 texRect{0.0f, 0.0f, 1.0f, 1.0f};
  glm::vec2 size{0.0f};  // pixels
};

// One tile of a tileset: how it looks and what it means.
struct Tile {
  std::string type;  // Tiled's "type" (class): what scripts call it
  nlohmann::json properties = nlohmann::json::object();  // its custom properties, by name ("solid": true...)
  TileImage image;
  // Tiled animation: frames of this tileset's tiles (local ids), seconds each.
  std::vector<std::pair<uint32_t, float>> animation;

  // A bool property ("solid", "deadly"), if the tile sets it.
  std::optional<bool> flag(std::string_view name) const;
};

// A Tiled tileset (.tsj, or one embedded in a map): one image cut into a grid,
// or a collection of images (each tile its own, optionally a sub-rectangle
// of it). Tiles are addressed by local id.
class Tileset {
 public:
  // An image by project path, or a sub-rectangle of it (x, y, w, h in pixels,
  // y down); nullopt if it can't be found.
  using ResolveImage = std::function<std::optional<TileImage>(const std::string& path, std::optional<glm::ivec4> rect)>;

  // `path` is the tileset's project path (images resolve relative to it);
  // unresolvable images go to `onMissing` and stay blank.
  static Tileset parse(const nlohmann::json& json, const std::string& path, const ResolveImage& resolve,
                       const std::function<void(const std::string&)>& onMissing = {});

  const Tile* tile(uint32_t id) const { return id < _tiles.size() && _tiles[id] ? &*_tiles[id] : nullptr; }
  // The tile's look at `time` seconds (its animation frame, if it has one).
  const Tile* frame(uint32_t id, float time) const;
  uint32_t tileCount() const { return static_cast<uint32_t>(_tiles.size()); }
  // The first tile whose type is `type`, or nullopt.
  std::optional<uint32_t> find(std::string_view type) const;
  glm::vec2 tileSize() const { return _tileSize; }
  glm::vec2 offset() const { return _offset; }  // Tiled "tileoffset", pixels (y down)
  const std::string& name() const { return _name; }

 private:
  std::vector<std::optional<Tile>> _tiles;
  glm::vec2 _tileSize{16.0f};
  glm::vec2 _offset{0.0f};
  std::string _name;
};

// Tiled's custom properties ([{name, type, value}]) as {name: value}.
nlohmann::json tiledProperties(const nlohmann::json& list);

// `relative` (as written in a Tiled file) resolved against the directory of
// `file`, both project paths: "maps/../tiles/a.png" → "tiles/a.png".
std::string resolveTiledPath(const std::string& file, const std::string& relative);
