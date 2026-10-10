#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Tileset.hpp"

// Tiled's global tile id flags (the top bits of a gid).
namespace gid {
constexpr uint32_t FlipH = 0x80000000u, FlipV = 0x40000000u, FlipD = 0x20000000u, Rotate = 0x10000000u;
constexpr uint32_t Flags = FlipH | FlipV | FlipD | Rotate;
}  // namespace gid

struct TileLayer {
  std::string name;
  std::vector<uint32_t> gids;  // Tiled order: the top row first
  bool visible = true;
  float opacity = 1.0f;
  glm::vec4 tint{1.0f};
  glm::vec2 offset{0.0f};    // pixels, y up
  glm::vec2 parallax{1.0f};  // 1 moves with the map; 0 stays put on screen
  float z = 0.0f;            // added to the map entity's z
};

// A placed shape from an object layer, in map pixels, y up from the bottom-left.
struct MapObject {
  int id = 0;
  std::string name, type, layer;
  glm::vec2 position{0.0f};  // its bottom-left corner
  glm::vec2 size{0.0f};
  bool point = false;
  uint32_t gid = 0;  // a tile object's tile (with flip flags), else 0
  bool visible = true;
  nlohmann::json properties = nlohmann::json::object();
  std::vector<glm::vec2> points;  // a polyline's or polygon's, in map pixels (y up); position is its anchor
  bool closed = false;            // a polygon: the last point joins the first
};

struct ImageLayer {
  std::string name;
  TileImage image;
  bool visible = true;
  float opacity = 1.0f;
  glm::vec4 tint{1.0f};
  glm::vec2 offset{0.0f};  // its top-left corner from the map's top-left, pixels, y up
  glm::vec2 parallax{1.0f};
  bool repeatX = false, repeatY = false;
  float z = 0.0f;
};

// A Tiled map (.tmj): tile layers over tilesets, object and image layers, and
// the questions games ask of it: what is where, what blocks, how a box moves
// through it. Tile (0, 0) is the bottom-left; positions are relative to the
// map's bottom-left corner, y up. Tiles are known by their type (Tiled "type").
class TileGrid {
 public:
  using LoadTileset = std::function<std::shared_ptr<const Tileset>(const std::string& path)>;

  TileGrid() = default;
  // `path` is the map's project path (tilesets and images resolve relative to it).
  // Problems (a missing tileset, an unsupported encoding) go to `onError`.
  static TileGrid parse(const nlohmann::json& map, const std::string& path, const LoadTileset& loadTileset,
                        const Tileset::ResolveImage& resolve, const std::function<void(const std::string&)>& onError = {});

  int width() const { return _width; }
  int height() const { return _height; }
  glm::vec2 tileSize() const { return _tileSize; }
  glm::vec2 pixelSize() const { return glm::vec2(_width, _height) * _tileSize; }

  // The type of the topmost tile at (tx, ty) ("" for none); beyond the edges, the map's "outside".
  std::string at(int tx, int ty) const;
  // Puts the first tile of `type` at (tx, ty) on tile layer `layer`, or by
  // default the topmost layer holding a tile there (else the first); "" clears
  // it. False if no tileset has `type` or there's no such layer.
  bool set(int tx, int ty, std::string_view type, std::string_view layer = {});
  // Shows or hides every layer named `name` (tiles, images, objects); false if none is.
  bool showLayer(std::string_view name, bool visible);
  bool solid(int tx, int ty) const { return is(tx, ty, "solid"); }
  // A bool property of the tiles at (tx, ty) ("solid", "deadly"): the topmost
  // tile that sets it decides (a bridge's false over solid water), else false.
  bool is(int tx, int ty, std::string_view property) const;
  // Every cell holding a tile of `type` on any layer, bottom row first.
  std::vector<glm::ivec2> positionsOf(std::string_view type) const;
  glm::ivec2 tileOf(glm::vec2 local) const { return glm::ivec2(glm::floor(local / _tileSize)); }

  // A tile as drawn: its tileset and local id, from a gid (flags ignored); nulls for none.
  struct Resolved {
    const Tileset* tileset = nullptr;
    uint32_t id = 0;
  };
  Resolved resolve(uint32_t gid) const;
  uint32_t gidAt(const TileLayer& layer, int tx, int ty) const;

  const std::vector<TileLayer>& layers() const { return _layers; }
  const std::vector<ImageLayer>& imageLayers() const { return _imageLayers; }
  const std::vector<MapObject>& objects() const { return _objects; }
  // The ground drawn on object layers: each line of every object whose class
  // is "ground" (solid) or "platform" (one-way), a polyline, polygon or
  // rectangle, as visit(a, b, oneWay) in map pixels. Hidden ones count too.
  template <typename Visit>
  void forEachTerrainLine(Visit visit) const {
    for (const MapObject& o : _objects) {
      if (o.type != "ground" && o.type != "platform") continue;
      const bool oneWay = o.type == "platform";
      std::vector<glm::vec2> corners = o.points;
      bool closed = o.closed;
      if (corners.empty() && !o.point && o.gid == 0 && o.size.x > 0 && o.size.y > 0) {
        corners = {o.position, o.position + glm::vec2(o.size.x, 0), o.position + o.size, o.position + glm::vec2(0, o.size.y)};
        closed = true;
      }
      const size_t n = corners.size();
      for (size_t i = 0; i + 1 < n + (closed && n > 2 ? 1 : 0); ++i) visit(corners[i], corners[(i + 1) % n], oneWay);
    }
  }
  const nlohmann::json& properties() const { return _properties; }

  // A box (center, half size) moved by `delta` one axis at a time, stopping
  // flush against solid tiles. With `slide` > 0, a move blocked along one axis
  // nudges sideways (up to `slide` units) toward an opening, so doorways are
  // easy to enter. `hit` is -1/+1 for the side blocked on each axis, and
  // `hitTile` the solid tile met last (nearest the box's center), or (-1, -1).
  struct Move {
    glm::vec2 position;
    glm::ivec2 hit{0, 0};
    glm::ivec2 hitTile{-1, -1};
  };
  Move move(glm::vec2 center, glm::vec2 half, glm::vec2 delta, float slide = 0.0f) const;

 private:
  struct TilesetRef {
    uint32_t firstGid;
    std::shared_ptr<const Tileset> tileset;
  };
  int _width = 0, _height = 0;
  glm::vec2 _tileSize{16.0f};
  std::vector<TilesetRef> _tilesets;  // by first gid
  std::vector<TileLayer> _layers;     // bottom to top
  std::vector<ImageLayer> _imageLayers;
  std::vector<MapObject> _objects;
  nlohmann::json _properties = nlohmann::json::object();
  // The tile beyond each edge (map properties "outside", or "outsideLeft"...), by type.
  struct {
    std::string left, right, top, bottom;
  } _outside;

  bool inside(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < _width && ty < _height; }
  size_t index(int tx, int ty) const { return static_cast<size_t>((_height - 1 - ty) * _width + tx); }
  const Tile* tile(uint32_t gid) const;
  const std::string& outside(int tx, int ty) const;
  const Tile* typed(std::string_view type) const;
  uint32_t gidOf(std::string_view type) const;
  bool overlapsSolid(glm::vec2 center, glm::vec2 half) const;
  void moveAxis(Move& m, glm::vec2 half, int axis, float delta, float slide) const;
};
