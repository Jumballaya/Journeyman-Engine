#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Project.hpp"

// Tiled maps (.tmj) and tilesets (.tsj) as the editor reads and changes them:
// JSON in Tiled's own format, so files go back and forth with Tiled untouched.
// Paths inside them are relative to the file; these take and give project paths.
// Cells count from the bottom-left (0, 0), as the engine does.
namespace tiled {

// Tiled's gid flags (the top bits): flips, and the hexagonal rotation.
inline constexpr uint32_t kFlipH = 0x80000000u, kFlipV = 0x40000000u, kFlipD = 0x20000000u, kFlags = 0xF0000000u;

Json newMap(glm::ivec2 size, glm::ivec2 tileSize);
Json newTileset(const std::string& name, glm::ivec2 tileSize);
// A .tmj as written: Tiled's layout, each tile layer one map row per line.
std::string serializeMap(const Json& map);

// Project paths a Tiled file at `path` uses (its tilesets, images).
std::vector<std::string> references(const Json& file, const std::string& path);
// A Tiled file moved from `oldPath` to `newPath`, while `moved` maps every
// project path to where it went: rewrites its relative references. True if any changed.
bool rebase(Json& file, const std::string& oldPath, const std::string& newPath,
            const std::function<std::string(const std::string&)>& moved);
// `target` (a project path) as a Tiled file at `file` writes it.
std::string relativeTo(const std::string& file, const std::string& target);

// ---- Tilesets ----

// What a tile looks like: an image (project path) or a rectangle of one (x, y, w, h; y down).
struct TileLook {
  std::string image;
  std::optional<glm::ivec4> rect;
};
// The ids a tileset has, in order (a sheet: every cell; a collection: its tiles).
std::vector<uint32_t> tileIds(const Json& tileset);
// One past the highest id: the gids a map gives the tileset.
uint32_t tileSpan(const Json& tileset);
std::optional<TileLook> lookOf(const Json& tileset, const std::string& path, uint32_t id);
// The tiles[] entry for `id`, made if `create`; null if there is none.
Json* tileEntry(Json& tileset, uint32_t id, bool create);
const Json* tileEntry(const Json& tileset, uint32_t id);
std::string typeOf(const Json& tileset, uint32_t id);
// A tile property's value (Tiled's [{name, type, value}]), or null.
Json property(const Json& holder, const std::string& name);
// Sets a property (null removes it); the type follows the value.
void setProperty(Json& holder, const std::string& name, const Json& value);

// ---- Maps ----

int width(const Json& map);
int height(const Json& map);
glm::ivec2 tileSize(const Json& map);
// Why the editor can't paint this map ("" if it can): infinite, compressed layers...
std::string uneditable(const Json& map);

struct TilesetRef {
  uint32_t firstGid;
  std::string path;  // project path; "" for one embedded in the map
};
std::vector<TilesetRef> tilesets(const Json& map, const std::string& mapPath);
// Adds a tileset (unless it's there) after the others' gids (`spanOf` a
// tileset path: its tileSpan); returns its first gid.
uint32_t addTileset(Json& map, const std::string& mapPath, const std::string& tilesetPath,
                    const std::function<uint32_t(const std::string& path)>& spanOf);
// Removes it, clearing its tiles from every layer.
void removeTileset(Json& map, const std::string& mapPath, const std::string& tilesetPath);

bool isTileLayer(const Json& layer);
bool isObjectLayer(const Json& layer);
// The top-level layer with this Tiled id, or -1.
int layerIndex(const Json& map, int id);
// Adds a layer ("tilelayer" or "objectgroup") above `below` (or on top); returns its index.
int addLayer(Json& map, const std::string& type, const std::string& name, int below = -1);
void moveLayer(Json& map, int from, int to);

uint32_t gidAt(const Json& map, int layer, glm::ivec2 cell);
// False if outside the map or unchanged.
bool setGid(Json& map, int layer, glm::ivec2 cell, uint32_t gid);
// Floods the 4-connected region holding the cell's gid; false if it already is `gid`.
bool fill(Json& map, int layer, glm::ivec2 cell, uint32_t gid);
// New size, the bottom-left corner staying put (rows grow and shrink at the top).
void resize(Json& map, glm::ivec2 size);

// Objects, in map pixels with y up: x, y is the bottom-left corner.
glm::vec4 objectRect(const Json& map, const Json& object);
void setObjectRect(const Json& map, Json& object, glm::vec4 rect);
// Adds a rectangle object to an object layer; returns its id.
int addObject(Json& map, int layer, glm::vec4 rect, const std::string& type);
// The object with this id (and its layer), or null.
Json* findObject(Json& map, int id, int* layer = nullptr);
// The topmost visible object under a point (map pixels, y up), or 0.
int objectAt(const Json& map, glm::vec2 point);
void removeObject(Json& map, int id);

// ---- Terrain (Tiled wang sets) ----

// Paints wang set `set`'s `color` (1-based; 0 erases) of the tileset at
// `firstGid` into a cell of a tile layer, then picks the cell's and its
// neighbours' tiles so their edges and corners meet as the set describes:
// edge sets join cells painted with a color (paths, walls), corner sets
// paint the corners of cells (Tiled's terrain brush on blob tilesets).
// False if nothing changed.
bool paintTerrain(Json& map, int layer, glm::ivec2 cell, const Json& tileset, uint32_t firstGid, int set, int color);

}  // namespace tiled
