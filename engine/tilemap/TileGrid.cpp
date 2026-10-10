#include "TileGrid.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {

constexpr float kGap = 0.01f;  // left between a stopped box and the tile it touches

std::vector<uint8_t> decodeBase64(std::string_view text) {
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
    else continue;  // padding, whitespace
    buffer = (buffer << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

// "#rrggbb" or "#aarrggbb" (Tiled's order) as rgba 0..1; white if absent.
glm::vec4 tiledColor(const nlohmann::json& value) {
  if (!value.is_string()) return glm::vec4(1.0f);
  std::string hex = value.get<std::string>();
  if (hex.starts_with('#')) hex.erase(0, 1);
  if (hex.size() != 6 && hex.size() != 8) return glm::vec4(1.0f);
  auto byte = [&](size_t at) { return static_cast<float>(std::stoi(hex.substr(at, 2), nullptr, 16)) / 255.0f; };
  const size_t rgb = hex.size() == 8 ? 2 : 0;
  return {byte(rgb), byte(rgb + 2), byte(rgb + 4), hex.size() == 8 ? byte(0) : 1.0f};
}

// What a layer passes to the layers inside it (Tiled group layers).
struct Inherited {
  bool visible = true;
  float opacity = 1.0f;
  glm::vec4 tint{1.0f};
  glm::vec2 offset{0.0f};
  glm::vec2 parallax{1.0f};
};

// A point of an object given from its anchor in Tiled's frame (y down), turned
// with the object (Tiled: clockwise, degrees), as map pixels y up.
glm::vec2 placed(glm::vec2 anchor, glm::vec2 local, float degrees) {
  const float r = glm::radians(degrees), c = std::cos(r), s = std::sin(r);
  return anchor + glm::vec2(local.x * c - local.y * s, -(local.x * s + local.y * c));
}

}  // namespace

TileGrid TileGrid::parse(const nlohmann::json& map, const std::string& path, const LoadTileset& loadTileset,
                         const Tileset::ResolveImage& resolve, const std::function<void(const std::string&)>& onError) {
  static std::atomic<uint64_t> parses{0};
  TileGrid grid;
  grid._revision = ++parses;
  auto error = [&](const std::string& message) {
    if (onError) onError(path + ": " + message);
  };
  if (!map.is_object()) return grid;
  if (map.value("infinite", false)) error("infinite maps aren't supported (Map > Map Properties > Infinite off)");
  if (const auto o = map.value("orientation", std::string("orthogonal")); o != "orthogonal") error(o + " maps aren't supported");
  grid._width = std::max(0, map.value("width", 0));
  grid._height = std::max(0, map.value("height", 0));
  grid._tileSize = {std::max(1.0f, map.value("tilewidth", 16.0f)), std::max(1.0f, map.value("tileheight", 16.0f))};
  grid._properties = tiledProperties(map.value("properties", nlohmann::json()));
  auto side = [&](const char* key) {
    const auto v = grid._properties.value(key, grid._properties.value("outside", nlohmann::json()));
    return v.is_string() ? v.get<std::string>() : std::string();
  };
  grid._outside = {side("outsideLeft"), side("outsideRight"), side("outsideTop"), side("outsideBottom")};

  for (const auto& ref : map.value("tilesets", nlohmann::json::array())) {
    const auto firstGid = ref.value("firstgid", 1u);
    std::shared_ptr<const Tileset> set;
    if (const std::string source = ref.value("source", std::string()); !source.empty()) {
      set = loadTileset(resolveTiledPath(path, source));
      if (!set) error("tileset '" + source + "' can't be read");
    } else {
      set = std::make_shared<const Tileset>(Tileset::parse(ref, path, resolve, [&](const std::string& f) { error("image '" + f + "' not found"); }));
    }
    if (set) grid._tilesets.push_back({firstGid, std::move(set)});
  }
  std::sort(grid._tilesets.begin(), grid._tilesets.end(), [](const auto& a, const auto& b) { return a.firstGid < b.firstGid; });

  const float mapHeight = grid.pixelSize().y;
  float depth = 0.0f;  // layers stack upward in z, in file order, unless a "z" property says otherwise
  auto layerZ = [&](const nlohmann::json& props) {
    const float z = props.value("z", nlohmann::json()).is_number() ? props["z"].get<float>() : depth;
    depth += 0.01f;
    return z;
  };
  const size_t cells = static_cast<size_t>(grid._width) * static_cast<size_t>(grid._height);

  std::function<void(const nlohmann::json&, const Inherited&)> layer = [&](const nlohmann::json& l, const Inherited& parent) {
    Inherited own;
    own.visible = parent.visible && l.value("visible", true);
    own.opacity = parent.opacity * l.value("opacity", 1.0f);
    own.tint = parent.tint * tiledColor(l.value("tintcolor", nlohmann::json()));
    own.offset = parent.offset + glm::vec2(l.value("offsetx", 0.0f), -l.value("offsety", 0.0f));
    own.parallax = parent.parallax * glm::vec2(l.value("parallaxx", 1.0f), l.value("parallaxy", 1.0f));
    const auto props = tiledProperties(l.value("properties", nlohmann::json()));
    const std::string name = l.value("name", std::string()), type = l.value("type", std::string());

    if (type == "group") {
      for (const auto& child : l.value("layers", nlohmann::json::array())) layer(child, own);
    } else if (type == "tilelayer") {
      TileLayer t{name, {}, own.visible, own.opacity, own.tint, own.offset, own.parallax, layerZ(props)};
      const auto data = l.value("data", nlohmann::json());
      if (data.is_array()) {
        t.gids = data.get<std::vector<uint32_t>>();
      } else if (data.is_string()) {
        if (!l.value("compression", std::string()).empty()) {
          error("layer '" + name + "' is compressed: save with Tile Layer Format CSV or Base64 (uncompressed)");
        }
        const auto bytes = decodeBase64(data.get<std::string>());
        for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
          t.gids.push_back(bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) | (static_cast<uint32_t>(bytes[i + 3]) << 24));
        }
      }
      t.gids.resize(cells, 0);
      grid._layers.push_back(std::move(t));
    } else if (type == "objectgroup") {
      const float z = layerZ(props);
      for (const auto& o : l.value("objects", nlohmann::json::array())) {
        MapObject obj;
        obj.id = o.value("id", 0);
        obj.name = o.value("name", std::string());
        obj.type = o.value("type", o.value("class", std::string()));
        obj.layer = name;
        obj.size = {o.value("width", 0.0f), o.value("height", 0.0f)};
        obj.point = o.value("point", false);
        obj.gid = o.value("gid", 0u);
        obj.visible = own.visible && o.value("visible", true);
        obj.properties = tiledProperties(o.value("properties", nlohmann::json()));
        // Tiled's y runs down; a tile object's y is its bottom, any other's its top.
        const float x = o.value("x", 0.0f), y = o.value("y", 0.0f), rotation = o.value("rotation", 0.0f);
        const glm::vec2 anchor = glm::vec2(x, mapHeight - y) + own.offset;
        obj.closed = o.contains("polygon");
        for (const auto& p : o.value(obj.closed ? "polygon" : "polyline", nlohmann::json::array())) {
          obj.points.push_back(placed(anchor, {p.value("x", 0.0f), p.value("y", 0.0f)}, rotation));
        }
        obj.position = !obj.points.empty() || obj.gid ? anchor : anchor - glm::vec2(0.0f, obj.size.y);
        if (obj.type == "ground" || obj.type == "platform") {
          const bool oneWay = obj.type == "platform";
          if (!obj.points.empty()) {
            grid._terrain.emplace_back(obj.points, obj.closed, oneWay);
          } else if (obj.point || obj.gid || o.value("ellipse", false)) {
            error("object " + std::to_string(obj.id) + " can't be " + obj.type + ": draw a polyline, polygon or rectangle");
          } else {
            const glm::vec2 w(obj.size.x, 0.0f), h(0.0f, obj.size.y);
            std::vector<glm::vec2> corners;
            for (const glm::vec2 c : {glm::vec2(0.0f), w, w + h, h}) corners.push_back(placed(anchor, c, rotation));
            if (oneWay) corners.resize(2);  // a platform is its top edge
            grid._terrain.emplace_back(std::move(corners), !oneWay, oneWay);
          }
        }
        obj.properties["z"] = obj.properties.value("z", z);
        grid._objects.push_back(std::move(obj));
      }
    } else if (type == "imagelayer") {
      const std::string image = l.value("image", std::string());
      auto found = image.empty() ? std::nullopt : resolve(resolveTiledPath(path, image), std::nullopt);
      if (!image.empty() && !found) error("image '" + image + "' not found");
      grid._imageLayers.push_back({name, found.value_or(TileImage{}), own.visible, own.opacity, own.tint, own.offset,
                                   own.parallax, l.value("repeatx", false), l.value("repeaty", false), layerZ(props)});
    }
  };
  for (const auto& l : map.value("layers", nlohmann::json::array())) layer(l, Inherited{});
  return grid;
}

TileGrid::Resolved TileGrid::resolve(uint32_t gid) const {
  gid &= ~gid::Flags;
  if (gid == 0) return {};
  // The last tileset starting at or before `gid`.
  auto it = std::upper_bound(_tilesets.begin(), _tilesets.end(), gid, [](uint32_t g, const TilesetRef& r) { return g < r.firstGid; });
  if (it == _tilesets.begin()) return {};
  --it;
  return {it->tileset.get(), gid - it->firstGid};
}

const Tile* TileGrid::tile(uint32_t gid) const {
  const Resolved r = resolve(gid);
  return r.tileset ? r.tileset->tile(r.id) : nullptr;
}

uint32_t TileGrid::gidAt(const TileLayer& layer, int tx, int ty) const { return inside(tx, ty) ? layer.gids[index(tx, ty)] : 0; }

const std::string& TileGrid::outside(int tx, int ty) const {
  if (tx < 0) return _outside.left;
  if (tx >= _width) return _outside.right;
  return ty < 0 ? _outside.bottom : _outside.top;
}

const Tile* TileGrid::typed(std::string_view type) const {
  return type.empty() ? nullptr : tile(gidOf(type));
}

uint32_t TileGrid::gidOf(std::string_view type) const {
  for (const auto& [first, set] : _tilesets) {
    if (auto id = set->find(type)) return first + *id;
  }
  return 0;
}

std::string TileGrid::at(int tx, int ty) const {
  if (!inside(tx, ty)) return outside(tx, ty);
  for (auto it = _layers.rbegin(); it != _layers.rend(); ++it) {
    if (const Tile* t = tile(it->gids[index(tx, ty)])) return t->type;
  }
  return {};
}

bool TileGrid::set(int tx, int ty, std::string_view type, std::string_view layer) {
  if (!inside(tx, ty)) return false;
  const uint32_t g = gidOf(type);
  if (!type.empty() && g == 0) return false;
  const size_t i = index(tx, ty);
  auto target = layer.empty()
                    ? std::find_if(_layers.rbegin(), _layers.rend(), [&](const TileLayer& l) { return l.gids[i] != 0; })
                    : std::find_if(_layers.rbegin(), _layers.rend(), [&](const TileLayer& l) { return l.name == layer; });
  if (target == _layers.rend()) {
    if (!layer.empty() || _layers.empty()) return false;
    target = std::prev(_layers.rend());  // the first layer
  }
  target->gids[i] = g;
  return true;
}

bool TileGrid::showLayer(std::string_view name, bool visible) {
  bool found = false;
  auto apply = [&](bool match, bool& shown) {
    if (match) shown = visible, found = true;
  };
  for (auto& l : _layers) apply(l.name == name, l.visible);
  for (auto& l : _imageLayers) apply(l.name == name, l.visible);
  for (auto& o : _objects) apply(o.layer == name, o.visible);
  return found;
}

bool TileGrid::is(int tx, int ty, std::string_view property) const {
  if (!inside(tx, ty)) {
    const Tile* t = typed(outside(tx, ty));
    return t && t->flag(property).value_or(false);
  }
  for (auto it = _layers.rbegin(); it != _layers.rend(); ++it) {
    const Tile* t = tile(it->gids[index(tx, ty)]);
    if (auto v = t ? t->flag(property) : std::nullopt) return *v;
  }
  return false;
}

std::vector<glm::ivec2> TileGrid::positionsOf(std::string_view type) const {
  std::vector<glm::ivec2> out;
  for (int ty = 0; ty < _height; ++ty) {
    for (int tx = 0; tx < _width; ++tx) {
      const bool found = std::any_of(_layers.begin(), _layers.end(), [&](const TileLayer& l) {
        const Tile* t = tile(l.gids[index(tx, ty)]);
        return t && t->type == type;
      });
      if (found) out.emplace_back(tx, ty);
    }
  }
  return out;
}

bool TileGrid::overlapsSolid(glm::vec2 center, glm::vec2 half) const {
  // Every tile past an edge is that side's `outside` tile, so one ring beyond the grid stands for all of them.
  auto range = [&](float low, float high, int axis, int size) {
    auto cell = [&](float at) { return static_cast<int>(std::clamp(std::floor(at / _tileSize[axis]), -1.0f, static_cast<float>(size))); };
    return std::pair(cell(low), cell(high));
  };
  const auto [x0, x1] = range(center.x - half.x, center.x + half.x, 0, _width);
  const auto [y0, y1] = range(center.y - half.y, center.y + half.y, 1, _height);
  for (int ty = y0; ty <= y1; ++ty) {
    for (int tx = x0; tx <= x1; ++tx) {
      if (solid(tx, ty)) return true;
    }
  }
  return false;
}

TileGrid::Move TileGrid::move(glm::vec2 center, glm::vec2 half, glm::vec2 delta, float slide) const {
  Move m{center};
  for (float v : {center.x, center.y, half.x, half.y, delta.x, delta.y}) {
    if (!std::isfinite(v)) return m;  // a script's NaN would otherwise be undefined int casts
  }
  moveAxis(m, half, 0, delta.x, delta.y == 0.0f ? slide : 0.0f);
  moveAxis(m, half, 1, delta.y, delta.x == 0.0f ? slide : 0.0f);
  return m;
}

void TileGrid::moveAxis(Move& m, glm::vec2 half, int axis, float delta, float slide) const {
  if (delta == 0.0f) return;
  const int other = 1 - axis;
  const float sign = delta > 0.0f ? 1.0f : -1.0f;
  const float size = _tileSize[axis];
  auto cellOf = [&](float at, int a) { return static_cast<int>(std::floor(at / _tileSize[a])); };
  // Steps of under half a tile, so fast boxes can't skip a thin wall.
  const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / (size * 0.45f))));
  const float step = delta / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i) {
    glm::vec2 next = m.position;
    next[axis] += step;
    if (!overlapsSolid(next, half)) {
      m.position = next;
      continue;
    }
    // Flush against the blocking tile row/column, and remember the tile.
    const int line = cellOf(next[axis] + sign * half[axis], axis);
    m.position[axis] = sign > 0.0f ? static_cast<float>(line) * size - half[axis] - kGap
                                   : static_cast<float>(line + 1) * size + half[axis] + kGap;
    m.hit[axis] = static_cast<int>(sign);
    const int from = cellOf(m.position[other] - half[other], other), to = cellOf(m.position[other] + half[other], other);
    const int middle = cellOf(m.position[other], other);
    int best = -1;
    for (int t = from; t <= to; ++t) {
      const bool isSolid = axis == 0 ? solid(line, t) : solid(t, line);
      if (isSolid && (best < 0 || std::abs(t - middle) < std::abs(best - middle))) best = t;
    }
    m.hitTile = axis == 0 ? glm::ivec2(line, best) : glm::ivec2(best, line);

    // Nudge toward the nearest opening that would let the move through.
    for (float off = 1.0f; off <= slide; off += 1.0f) {
      for (float sideSign : {-1.0f, 1.0f}) {
        glm::vec2 probe = next;
        probe[other] += sideSign * off;
        if (overlapsSolid(probe, half)) continue;
        m.position[other] += sideSign * std::min(off, std::fabs(delta));
        return;
      }
    }
    return;
  }
}
