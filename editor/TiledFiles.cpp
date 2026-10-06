#include "TiledFiles.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>

#include "tilemap/Tileset.hpp"

namespace fs = std::filesystem;

namespace tiled {

namespace {

// Calls `visit` with each relative path a Tiled file holds (tileset sources, images).
void forEachReference(Json& file, const std::function<void(Json& reference)>& visit) {
  auto image = [&](Json& holder, const char* key) {
    if (holder.is_object() && holder.contains(key) && holder[key].is_string() && !holder[key].get<std::string>().empty()) {
      visit(holder[key]);
    }
  };
  auto tileset = [&](Json& set) {
    image(set, "image");
    if (set.contains("tiles") && set["tiles"].is_array()) {
      for (Json& t : set["tiles"]) image(t, "image");
    }
  };
  std::function<void(Json&)> layers = [&](Json& list) {
    if (!list.is_array()) return;
    for (Json& l : list) {
      image(l, "image");
      if (l.contains("layers")) layers(l["layers"]);
    }
  };
  if (!file.is_object()) return;
  if (!file.contains("layers")) return tileset(file);
  layers(file["layers"]);
  if (!file.contains("tilesets") || !file["tilesets"].is_array()) return;
  for (Json& ref : file["tilesets"]) {
    if (ref.contains("source")) image(ref, "source");
    else tileset(ref);
  }
}

uint32_t decodeLittle(const std::vector<uint8_t>& bytes, size_t at) {
  return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

std::vector<uint8_t> decodeBase64(const std::string& text) {
  std::vector<uint8_t> out;
  uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;
    buffer = (buffer << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

// A tile layer's data as a gid array, decoding uncompressed base64 in place.
Json& dataOf(Json& layer, size_t cells) {
  Json& data = layer["data"];
  if (data.is_string() && layer.value("compression", std::string()).empty()) {
    const auto bytes = decodeBase64(data.get<std::string>());
    Json gids = Json::array();
    for (size_t i = 0; i + 3 < bytes.size(); i += 4) gids.push_back(decodeLittle(bytes, i));
    data = std::move(gids);
    layer.erase("encoding");
  }
  if (!data.is_array()) data = Json::array();
  while (data.size() < cells) data.push_back(0);
  return data;
}

const Json& layersOf(const Json& map) {
  static const Json none = Json::array();
  return map.is_object() && map.contains("layers") ? map.at("layers") : none;
}

size_t indexOf(const Json& map, glm::ivec2 cell) {
  return static_cast<size_t>((height(map) - 1 - cell.y) * width(map) + cell.x);
}

bool inside(const Json& map, glm::ivec2 cell) {
  return cell.x >= 0 && cell.y >= 0 && cell.x < width(map) && cell.y < height(map);
}

Json* tileLayer(Json& map, int layer) {
  if (!map.contains("layers") || layer < 0 || layer >= static_cast<int>(map["layers"].size())) return nullptr;
  Json& l = map["layers"][static_cast<size_t>(layer)];
  return isTileLayer(l) ? &l : nullptr;
}

// Every tile layer, groups included.
void forEachTileLayer(Json& layers, const std::function<void(Json&)>& visit) {
  for (Json& l : layers) {
    if (isTileLayer(l)) visit(l);
    if (l.contains("layers")) forEachTileLayer(l["layers"], visit);
  }
}

}  // namespace

Json newMap(glm::ivec2 size, glm::ivec2 tile) {
  Json layer = {{"id", 1}, {"name", "Ground"}, {"type", "tilelayer"}, {"x", 0}, {"y", 0}, {"width", size.x},
                {"height", size.y}, {"opacity", 1}, {"visible", true}, {"data", Json::array()}};
  layer["data"] = Json(std::vector<uint32_t>(static_cast<size_t>(size.x * size.y), 0));
  return {{"type", "map"}, {"version", "1.10"}, {"tiledversion", "1.12.2"}, {"orientation", "orthogonal"},
          {"renderorder", "right-down"}, {"infinite", false}, {"compressionlevel", -1}, {"width", size.x},
          {"height", size.y}, {"tilewidth", tile.x}, {"tileheight", tile.y}, {"nextlayerid", 2},
          {"nextobjectid", 1}, {"layers", Json::array({layer})}, {"tilesets", Json::array()}};
}

Json newTileset(const std::string& name, glm::ivec2 tile) {
  return {{"type", "tileset"}, {"version", "1.10"}, {"tiledversion", "1.12.2"}, {"name", name},
          {"tilewidth", tile.x}, {"tileheight", tile.y}, {"columns", 0}, {"margin", 0}, {"spacing", 0},
          {"tilecount", 0}, {"grid", {{"orientation", "orthogonal"}, {"width", 1}, {"height", 1}}},
          {"tiles", Json::array()}};
}

std::string serializeMap(const Json& map) {
  // Each layer's data becomes a placeholder string, swapped for rows of numbers after dumping.
  Json copy = map;
  std::map<std::string, std::string> rows;
  std::function<void(Json&)> walk = [&](Json& layers) {
    for (Json& l : layers) {
      if (l.contains("layers")) walk(l["layers"]);
      if (!isTileLayer(l) || !l["data"].is_array()) continue;
      const size_t w = std::max(1, l.value("width", width(map)));
      const Json& data = l["data"];
      std::string text = "[";
      for (size_t i = 0; i < data.size(); ++i) {
        text += i % w == 0 ? "\n    " : " ";
        text += data[i].dump() + (i + 1 < data.size() ? "," : "");
      }
      const std::string key = "@@rows" + std::to_string(rows.size()) + "@@";
      rows[key] = text + "\n   ]";
      l["data"] = key;
    }
  };
  if (copy.contains("layers")) walk(copy["layers"]);
  std::string text = copy.dump(1);
  for (const auto& [key, body] : rows) {
    const std::string quoted = "\"" + key + "\"";
    if (const size_t at = text.find(quoted); at != std::string::npos) text.replace(at, quoted.size(), body);
  }
  return text + "\n";
}

std::string relativeTo(const std::string& file, const std::string& target) {
  const fs::path base = fs::path(file).parent_path();
  return base.empty() ? target : fs::path(target).lexically_relative(base).generic_string();
}

std::vector<std::string> references(const Json& file, const std::string& path) {
  std::vector<std::string> out;
  Json copy = file;
  forEachReference(copy, [&](Json& ref) { out.push_back(resolveTiledPath(path, ref.get<std::string>())); });
  return out;
}

bool rebase(Json& file, const std::string& oldPath, const std::string& newPath,
            const std::function<std::string(const std::string&)>& moved) {
  bool changed = false;
  forEachReference(file, [&](Json& ref) {
    const std::string target = moved(resolveTiledPath(oldPath, ref.get<std::string>()));
    const std::string rewritten = relativeTo(newPath, target);
    if (rewritten != ref.get<std::string>()) ref = rewritten, changed = true;
  });
  return changed;
}

// ---- Tilesets ----

std::vector<uint32_t> tileIds(const Json& tileset) {
  std::vector<uint32_t> ids;
  if (tileset.value("image", std::string()).empty()) {
    for (const Json& t : tileset.value("tiles", Json::array())) {
      if (t.contains("image")) ids.push_back(t.value("id", 0u));
    }
    std::sort(ids.begin(), ids.end());
  } else {
    for (uint32_t id = 0; id < tileset.value("tilecount", 0u); ++id) ids.push_back(id);
  }
  return ids;
}

uint32_t tileSpan(const Json& tileset) {
  uint32_t span = tileset.value("tilecount", 0u);
  for (const Json& t : tileset.value("tiles", Json::array())) span = std::max(span, t.value("id", 0u) + 1);
  return span;
}

std::optional<TileLook> lookOf(const Json& tileset, const std::string& path, uint32_t id) {
  if (const std::string sheet = tileset.value("image", std::string()); !sheet.empty()) {
    if (id >= tileset.value("tilecount", 0u)) return std::nullopt;
    const int columns = std::max(1, tileset.value("columns", 1));
    const int margin = tileset.value("margin", 0), spacing = tileset.value("spacing", 0);
    const glm::ivec2 size(tileset.value("tilewidth", 16), tileset.value("tileheight", 16));
    const glm::ivec2 at = glm::ivec2(margin) + glm::ivec2(id % columns, id / columns) * (size + spacing);
    return TileLook{resolveTiledPath(path, sheet), glm::ivec4(at, size)};
  }
  const Json* entry = tileEntry(tileset, id);
  if (!entry || entry->value("image", std::string()).empty()) return std::nullopt;
  TileLook look{resolveTiledPath(path, (*entry)["image"].get<std::string>()), std::nullopt};
  if (entry->contains("width") && entry->contains("height")) {
    look.rect = glm::ivec4(entry->value("x", 0), entry->value("y", 0), (*entry)["width"].get<int>(), (*entry)["height"].get<int>());
  }
  return look;
}

const Json* tileEntry(const Json& tileset, uint32_t id) {
  if (!tileset.contains("tiles")) return nullptr;
  for (const Json& t : tileset["tiles"]) {
    if (t.value("id", ~0u) == id) return &t;
  }
  return nullptr;
}

Json* tileEntry(Json& tileset, uint32_t id, bool create) {
  if (!tileset.contains("tiles") || !tileset["tiles"].is_array()) {
    if (!create) return nullptr;
    tileset["tiles"] = Json::array();
  }
  Json& tiles = tileset["tiles"];
  size_t at = 0;
  for (; at < tiles.size(); ++at) {
    const uint32_t other = tiles[at].value("id", ~0u);
    if (other == id) return &tiles[at];
    if (other > id) break;
  }
  if (!create) return nullptr;
  tiles.insert(tiles.begin() + static_cast<std::ptrdiff_t>(at), Json{{"id", id}});
  return &tiles[at];
}

std::string typeOf(const Json& tileset, uint32_t id) {
  const Json* entry = tileEntry(tileset, id);
  return entry ? entry->value("type", entry->value("class", std::string())) : std::string();
}

Json property(const Json& holder, const std::string& name) {
  for (const Json& p : holder.value("properties", Json::array())) {
    if (p.value("name", std::string()) == name) return p.value("value", Json());
  }
  return Json();
}

void setProperty(Json& holder, const std::string& name, const Json& value) {
  Json list = holder.value("properties", Json::array());
  auto it = std::find_if(list.begin(), list.end(), [&](const Json& p) { return p.value("name", std::string()) == name; });
  if (value.is_null()) {
    if (it != list.end()) list.erase(it);
  } else {
    const char* type = value.is_boolean() ? "bool" : value.is_number_integer() ? "int" : value.is_number() ? "float" : "string";
    Json p = {{"name", name}, {"type", type}, {"value", value}};
    if (it == list.end()) list.push_back(std::move(p));
    else *it = std::move(p);
  }
  if (list.empty()) holder.erase("properties");
  else holder["properties"] = std::move(list);
}

// ---- Maps ----

int width(const Json& map) { return std::max(0, map.value("width", 0)); }
int height(const Json& map) { return std::max(0, map.value("height", 0)); }
glm::ivec2 tileSize(const Json& map) { return {std::max(1, map.value("tilewidth", 16)), std::max(1, map.value("tileheight", 16))}; }

std::string uneditable(const Json& map) {
  if (!map.is_object()) return "This isn't a Tiled map.";
  if (map.value("infinite", false)) return "This map is infinite: in Tiled, turn off Map > Map Properties > Infinite.";
  if (map.value("orientation", std::string("orthogonal")) != "orthogonal") return "Only orthogonal maps can be painted here.";
  for (const Json& l : layersOf(map)) {
    if (isTileLayer(l) && !l.value("compression", std::string()).empty()) {
      return "Layer '" + l.value("name", std::string()) + "' is compressed: in Tiled, set the map's Tile Layer Format to CSV.";
    }
  }
  return "";
}

std::vector<TilesetRef> tilesets(const Json& map, const std::string& mapPath) {
  std::vector<TilesetRef> out;
  for (const Json& t : map.value("tilesets", Json::array())) {
    const std::string source = t.value("source", std::string());
    out.push_back({t.value("firstgid", 1u), source.empty() ? std::string() : resolveTiledPath(mapPath, source)});
  }
  std::sort(out.begin(), out.end(), [](const TilesetRef& a, const TilesetRef& b) { return a.firstGid < b.firstGid; });
  return out;
}

uint32_t addTileset(Json& map, const std::string& mapPath, const std::string& tilesetPath,
                    const std::function<uint32_t(const std::string& path)>& spanOf) {
  if (!map.contains("tilesets") || !map["tilesets"].is_array()) map["tilesets"] = Json::array();
  uint32_t next = 1;
  for (const Json& t : map["tilesets"]) {
    const std::string source = t.value("source", std::string());
    const std::string path = source.empty() ? std::string() : resolveTiledPath(mapPath, source);
    if (!path.empty() && path == tilesetPath) return t.value("firstgid", 1u);
    next = std::max(next, t.value("firstgid", 1u) + std::max(1u, path.empty() ? tileSpan(t) : spanOf(path)));
  }
  map["tilesets"].push_back({{"firstgid", next}, {"source", relativeTo(mapPath, tilesetPath)}});
  return next;
}

void removeTileset(Json& map, const std::string& mapPath, const std::string& tilesetPath) {
  const auto refs = tilesets(map, mapPath);
  auto it = std::find_if(refs.begin(), refs.end(), [&](const TilesetRef& r) { return r.path == tilesetPath; });
  if (it == refs.end()) return;
  const uint32_t first = it->firstGid, end = std::next(it) == refs.end() ? ~0u : std::next(it)->firstGid;
  auto ours = [&](uint32_t gid) { gid &= ~kFlags; return gid >= first && gid < end; };
  forEachTileLayer(map["layers"], [&](Json& l) {
    for (Json& g : dataOf(l, 0)) {
      if (ours(g.get<uint32_t>())) g = 0;
    }
  });
  Json& list = map["tilesets"];
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].value("firstgid", 0u) == first) {
      list.erase(i);
      break;
    }
  }
}

bool isTileLayer(const Json& layer) { return layer.value("type", std::string()) == "tilelayer"; }
bool isObjectLayer(const Json& layer) { return layer.value("type", std::string()) == "objectgroup"; }

int layerIndex(const Json& map, int id) {
  const Json& layers = layersOf(map);
  for (size_t i = 0; i < layers.size(); ++i) {
    if (layers[i].value("id", -1) == id) return static_cast<int>(i);
  }
  return -1;
}

int addLayer(Json& map, const std::string& type, const std::string& name, int below) {
  const int id = map.value("nextlayerid", 1);
  map["nextlayerid"] = id + 1;
  Json layer = {{"id", id}, {"name", name}, {"type", type}, {"x", 0}, {"y", 0}, {"opacity", 1}, {"visible", true}};
  if (type == "tilelayer") {
    layer["width"] = width(map);
    layer["height"] = height(map);
    layer["data"] = Json(std::vector<uint32_t>(static_cast<size_t>(width(map) * height(map)), 0));
  } else {
    layer["draworder"] = "topdown";
    layer["objects"] = Json::array();
  }
  Json& layers = map["layers"];
  const int at = below < 0 || below >= static_cast<int>(layers.size()) ? static_cast<int>(layers.size()) : below + 1;
  layers.insert(layers.begin() + at, std::move(layer));
  return at;
}

void moveLayer(Json& map, int from, int to) {
  Json& layers = map["layers"];
  const int count = static_cast<int>(layers.size());
  if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;
  Json moving = layers[static_cast<size_t>(from)];
  layers.erase(static_cast<size_t>(from));
  layers.insert(layers.begin() + to, std::move(moving));
}

uint32_t gidAt(const Json& map, int layer, glm::ivec2 cell) {
  if (!inside(map, cell)) return 0;
  const Json& layers = layersOf(map);
  if (layer < 0 || layer >= static_cast<int>(layers.size()) || !isTileLayer(layers[static_cast<size_t>(layer)])) return 0;
  const Json& data = layers[static_cast<size_t>(layer)]["data"];
  const size_t i = indexOf(map, cell);
  return data.is_array() && i < data.size() ? data[i].get<uint32_t>() : 0;
}

bool setGid(Json& map, int layer, glm::ivec2 cell, uint32_t gid) {
  Json* l = tileLayer(map, layer);
  if (!l || !inside(map, cell)) return false;
  Json& slot = dataOf(*l, static_cast<size_t>(width(map) * height(map)))[indexOf(map, cell)];
  if (slot.get<uint32_t>() == gid) return false;
  slot = gid;
  return true;
}

bool fill(Json& map, int layer, glm::ivec2 cell, uint32_t gid) {
  if (!tileLayer(map, layer) || !inside(map, cell)) return false;
  const uint32_t from = gidAt(map, layer, cell);
  if (from == gid) return false;
  std::vector<glm::ivec2> stack{cell};
  while (!stack.empty()) {
    const glm::ivec2 c = stack.back();
    stack.pop_back();
    if (!inside(map, c) || gidAt(map, layer, c) != from) continue;
    setGid(map, layer, c, gid);
    for (glm::ivec2 d : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) stack.push_back(c + d);
  }
  return true;
}

void resize(Json& map, glm::ivec2 size) {
  const int w = width(map), h = height(map);
  size = glm::max(size, glm::ivec2(1));
  forEachTileLayer(map["layers"], [&](Json& l) {
    const Json old = dataOf(l, static_cast<size_t>(w * h));
    std::vector<uint32_t> next(static_cast<size_t>(size.x * size.y), 0);
    for (int y = 0; y < std::min(h, size.y); ++y) {
      for (int x = 0; x < std::min(w, size.x); ++x) {
        next[static_cast<size_t>((size.y - 1 - y) * size.x + x)] = old[static_cast<size_t>((h - 1 - y) * w + x)];
      }
    }
    l["data"] = Json(next);
    l["width"] = size.x;
    l["height"] = size.y;
  });
  // Objects are placed from the top: keep them where they stand over the bottom-left.
  const float shift = static_cast<float>((size.y - h) * tileSize(map).y);
  for (Json& l : map["layers"]) {
    if (!l.contains("objects")) continue;
    for (Json& o : l["objects"]) o["y"] = o.value("y", 0.0f) + shift;
  }
  map["width"] = size.x;
  map["height"] = size.y;
}

glm::vec4 objectRect(const Json& map, const Json& object) {
  const float mapHeight = static_cast<float>(height(map) * tileSize(map).y);
  const float w = object.value("width", 0.0f), h = object.value("height", 0.0f), y = object.value("y", 0.0f);
  // A tile object's y is its bottom; any other's its top.
  return {object.value("x", 0.0f), mapHeight - y - (object.value("gid", 0u) ? 0.0f : h), w, h};
}

void setObjectRect(const Json& map, Json& object, glm::vec4 rect) {
  const float mapHeight = static_cast<float>(height(map) * tileSize(map).y);
  object["x"] = rect.x;
  object["y"] = mapHeight - rect.y - (object.value("gid", 0u) ? 0.0f : rect.w);
  object["width"] = rect.z;
  object["height"] = rect.w;
}

int addObject(Json& map, int layer, glm::vec4 rect, const std::string& type) {
  if (layer < 0 || layer >= static_cast<int>(map["layers"].size()) || !isObjectLayer(map["layers"][static_cast<size_t>(layer)])) return 0;
  const int id = map.value("nextobjectid", 1);
  map["nextobjectid"] = id + 1;
  Json object = {{"id", id}, {"name", ""}, {"type", type}, {"x", 0}, {"y", 0}, {"width", 0}, {"height", 0}, {"rotation", 0}, {"visible", true}};
  setObjectRect(map, object, rect);
  map["layers"][static_cast<size_t>(layer)]["objects"].push_back(std::move(object));
  return id;
}

Json* findObject(Json& map, int id, int* layer) {
  if (!map.contains("layers")) return nullptr;
  for (size_t i = 0; i < map["layers"].size(); ++i) {
    Json& l = map["layers"][i];
    if (!isObjectLayer(l)) continue;
    for (Json& o : l["objects"]) {
      if (o.value("id", 0) != id) continue;
      if (layer) *layer = static_cast<int>(i);
      return &o;
    }
  }
  return nullptr;
}

int objectAt(const Json& map, glm::vec2 point) {
  const Json& layers = layersOf(map);
  for (auto l = layers.rbegin(); l != layers.rend(); ++l) {
    if (!isObjectLayer(*l) || !l->value("visible", true)) continue;
    const Json& objects = (*l)["objects"];
    for (auto o = objects.rbegin(); o != objects.rend(); ++o) {
      const glm::vec4 r = objectRect(map, *o);
      const float slop = r.z <= 0.0f || r.w <= 0.0f ? 4.0f : 0.0f;  // points and lines still take a click
      if (point.x >= r.x - slop && point.x <= r.x + r.z + slop && point.y >= r.y - slop && point.y <= r.y + r.w + slop) {
        return o->value("id", 0);
      }
    }
  }
  return 0;
}

void removeObject(Json& map, int id) {
  int layer = -1;
  if (!findObject(map, id, &layer)) return;
  Json& objects = map["layers"][static_cast<size_t>(layer)]["objects"];
  for (size_t i = 0; i < objects.size(); ++i) {
    if (objects[i].value("id", 0) == id) {
      objects.erase(i);
      return;
    }
  }
}

// ---- Terrain ----

namespace {

using Wang = std::array<int, 8>;  // top, top-right, right, bottom-right, bottom, bottom-left, left, top-left

// Wang positions as cell offsets.
constexpr glm::ivec2 kAround[8] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};

struct Terrain {
  Json& map;
  int layer;
  uint32_t firstGid;
  std::map<uint32_t, Wang> tiles;  // local tile id -> wang id
  bool edges, corners;             // which positions the set uses

  std::optional<Wang> wangAt(glm::ivec2 cell) const {
    const uint32_t gid = gidAt(map, layer, cell) & ~kFlags;
    if (gid < firstGid) return std::nullopt;
    auto it = tiles.find(gid - firstGid);
    return it == tiles.end() ? std::nullopt : std::optional(it->second);
  }

  // The tile best matching `want` (-1: any), of `color` if given (holding it, or
  // no color at all: a lone piece); varied by cell among equals.
  std::optional<uint32_t> best(const Wang& want, int color, glm::ivec2 cell) const {
    int bestScore = 1 << 30;
    std::vector<uint32_t> ties;
    for (const auto& [id, wang] : tiles) {
      const bool blank = std::all_of(wang.begin(), wang.end(), [](int c) { return c == 0; });
      if (color && !blank && std::find(wang.begin(), wang.end(), color) == wang.end()) continue;
      int score = 0;
      for (int k = 0; k < 8; ++k) {
        const bool used = k % 2 == 0 ? edges : corners;
        if (used && want[k] >= 0 && wang[k] != want[k]) ++score;
      }
      if (score < bestScore) bestScore = score, ties.clear();
      if (score == bestScore) ties.push_back(id);
    }
    if (ties.empty()) return std::nullopt;
    const uint32_t hash = static_cast<uint32_t>(cell.x) * 73856093u ^ static_cast<uint32_t>(cell.y) * 19349663u;
    return ties[hash % ties.size()];
  }
};

// The color a cell is painted with (an edge set's cell): its tile's most common
// color; a lone piece (no color) counts as the first.
int cellColor(const Terrain& t, glm::ivec2 cell) {
  const auto wang = t.wangAt(cell);
  if (!wang) return 0;
  std::map<int, int> counts;
  for (int k = 0; k < 8; ++k) {
    if ((*wang)[k] && (k % 2 == 0 ? t.edges : t.corners)) ++counts[(*wang)[k]];
  }
  int color = 1, most = 0;
  for (const auto& [c, n] : counts) {
    if (n > most) color = c, most = n;
  }
  return color;
}

// Edge (and mixed) sets: cells hold a color; a side (or corner) joins only neighbours of the same color.
bool paintCells(Terrain& t, glm::ivec2 target, int color) {
  std::map<std::pair<int, int>, int> painted{{{target.x, target.y}, color}};
  auto colorAt = [&](glm::ivec2 c) {
    if (!inside(t.map, c)) return -1;
    auto it = painted.find({c.x, c.y});
    return it != painted.end() ? it->second : cellColor(t, c);
  };
  bool changed = false;
  for (int k = -1; k < 8; ++k) {
    const glm::ivec2 cell = k < 0 ? target : target + kAround[k];
    const int own = colorAt(cell);
    if (own < 0 || (k >= 0 && own == 0)) continue;
    if (own == 0) {  // erased: only a tile of this set is taken away
      if (t.wangAt(cell)) changed |= setGid(t.map, t.layer, cell, 0);
      continue;
    }
    Wang want;
    for (int p = 0; p < 8; ++p) {
      const int n = colorAt(cell + kAround[p]);
      if (p % 2 == 0) {
        want[p] = n < 0 ? -1 : n == own ? own : 0;
      } else {
        const int a = colorAt(cell + kAround[(p + 7) % 8]), b = colorAt(cell + kAround[(p + 1) % 8]);
        want[p] = n < 0 || a < 0 || b < 0 ? -1 : n == own && a == own && b == own ? own : 0;
      }
    }
    if (auto id = t.best(want, own, cell)) changed |= setGid(t.map, t.layer, cell, t.firstGid + *id);
  }
  return changed;
}

// Corner sets: colors live on the grid's corners, which neighbouring cells share.
bool paintCorners(Terrain& t, glm::ivec2 target, int color) {
  // Cell corners as wang positions, and the corner point each sits on (from the cell's bottom-left).
  constexpr std::pair<int, glm::ivec2> kCorners[4] = {{1, {1, 1}}, {3, {1, 0}}, {5, {0, 0}}, {7, {0, 1}}};
  std::map<std::pair<int, int>, int> painted;
  for (const auto& [_, at] : kCorners) painted[{target.x + at.x, target.y + at.y}] = color;
  auto vertex = [&](glm::ivec2 v) {
    if (auto it = painted.find({v.x, v.y}); it != painted.end()) return it->second;
    // From whichever cell around the point has a tile of this set.
    for (const auto& [position, at] : kCorners) {
      if (auto wang = t.wangAt(v - at)) return (*wang)[position];
    }
    return 0;
  };
  bool changed = false;
  for (int k = -1; k < 8; ++k) {
    const glm::ivec2 cell = k < 0 ? target : target + kAround[k];
    if (!inside(t.map, cell)) continue;
    Wang want;
    want.fill(-1);
    bool any = false;
    for (const auto& [position, at] : kCorners) any |= (want[position] = vertex(cell + at)) != 0;
    if (!any) {
      if (t.wangAt(cell)) changed |= setGid(t.map, t.layer, cell, 0);
      continue;
    }
    if (auto id = t.best(want, 0, cell)) changed |= setGid(t.map, t.layer, cell, t.firstGid + *id);
  }
  return changed;
}

}  // namespace

bool paintTerrain(Json& map, int layer, glm::ivec2 cell, const Json& tileset, uint32_t firstGid, int set, int color) {
  const Json sets = tileset.value("wangsets", Json::array());
  if (!tileLayer(map, layer) || !inside(map, cell) || set < 0 || set >= static_cast<int>(sets.size())) return false;
  const Json& ws = sets[static_cast<size_t>(set)];
  const std::string type = ws.value("type", std::string("corner"));
  Terrain t{map, layer, firstGid, {}, type != "corner", type != "edge"};
  for (const Json& w : ws.value("wangtiles", Json::array())) {
    const Json id = w.value("wangid", Json::array());
    if (id.size() != 8) continue;
    Wang wang;
    for (size_t k = 0; k < 8; ++k) wang[k] = id[k].get<int>();
    t.tiles[w.value("tileid", 0u)] = wang;
  }
  if (t.tiles.empty()) return false;
  return type == "corner" ? paintCorners(t, cell, color) : paintCells(t, cell, color);
}

}  // namespace tiled
