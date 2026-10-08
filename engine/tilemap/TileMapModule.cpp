#include "TileMapModule.hpp"

#include <cmath>
#include <cstring>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "../core/logger/logging.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "../renderer2d/Renderer2DModule.hpp"
#include "TileMapComponent.hpp"

// Tile images come from the renderer.
template <>
struct ModuleTraits<TileMapModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag>;
};

REGISTER_MODULE(TileMapModule)

namespace {

constexpr int kOversizeMargin = 4;  // tiles left/below the view still drawn, for images bigger than a cell

// Draws each map's image layers, tile layers and tile objects in view, each at its entity's z plus its own.
class TileMapRenderSystem : public System {
 public:
  explicit TileMapRenderSystem(Renderer2D& renderer) : _renderer(renderer) {}

  void update(World& world, float dt) override {
    _time += dt;
    const Camera2D& camera = _renderer.camera();
    const glm::vec2 half = glm::vec2(_renderer.logicalSize()) * 0.5f / camera.zoom();
    for (auto [entity, map, transform] : world.view<TileMapComponent, TransformComponent>()) {
      const TileGrid& grid = map->grid;
      View view{camera.position() - half, camera.position() + half, glm::vec2(transform->position),
                transform->position.z};
      // Tiled's parallax pivots on the map's top-left (or its "parallaxorigin").
      view.pivot = view.origin + glm::vec2(0.0f, grid.pixelSize().y) +
                   glm::vec2(grid.properties().value("parallaxoriginx", 0.0f), -grid.properties().value("parallaxoriginy", 0.0f));
      view.camera = camera.position();
      for (const ImageLayer& layer : grid.imageLayers()) drawImageLayer(grid, view, layer);
      for (const TileLayer& layer : grid.layers()) drawTileLayer(grid, view, layer);
      for (const MapObject& object : grid.objects()) drawTileObject(grid, view, object);
    }
  }

  const char* name() const override { return "TileMapRenderSystem"; }

 private:
  struct View {
    glm::vec2 low, high;  // the world rectangle on screen
    glm::vec2 origin;     // the map's bottom-left
    float z;
    glm::vec2 pivot{0.0f}, camera{0.0f};

    // Where a layer with this offset and parallax puts the map's bottom-left.
    glm::vec2 layerOrigin(glm::vec2 offset, glm::vec2 parallax) const {
      return origin + offset + (camera - pivot) * (1.0f - parallax);
    }
  };

  Renderer2D& _renderer;
  float _time = 0.0f;

  void drawTileLayer(const TileGrid& grid, const View& view, const TileLayer& layer) {
    if (!layer.visible || layer.opacity <= 0.0f) return;
    const glm::vec2 base = view.layerOrigin(layer.offset, layer.parallax);
    const glm::ivec2 low = grid.tileOf(view.low - base) - kOversizeMargin, high = grid.tileOf(view.high - base) + 1;
    const glm::vec4 color = layer.tint * glm::vec4(1.0f, 1.0f, 1.0f, layer.opacity);
    for (int ty = std::max(0, low.y); ty <= std::min(grid.height() - 1, high.y); ++ty) {
      for (int tx = std::max(0, low.x); tx <= std::min(grid.width() - 1, high.x); ++tx) {
        const uint32_t gid = grid.gidAt(layer, tx, ty);
        const auto [tileset, id] = grid.resolve(gid);
        if (!tileset) continue;
        // Tiled grows a tile up and right from its cell's bottom-left, moved by the tileset's offset (y down).
        const glm::vec2 corner = base + glm::vec2(tx, ty) * grid.tileSize() + tileset->offset() * glm::vec2(1, -1);
        drawTile(tileset->frame(id, _time), gid, corner, std::nullopt, color, view.z + layer.z);
      }
    }
  }

  void drawTileObject(const TileGrid& grid, const View& view, const MapObject& object) {
    if (!object.visible || !object.gid) return;
    const auto [tileset, id] = grid.resolve(object.gid);
    if (!tileset) return;
    const float z = object.properties.value("z", 0.0f);
    std::optional<glm::vec2> size;
    if (object.size.x > 0.0f && object.size.y > 0.0f) size = object.size;
    drawTile(tileset->frame(id, _time), object.gid, view.origin + object.position, size, glm::vec4(1.0f), view.z + z);
  }

  void drawImageLayer(const TileGrid& grid, const View& view, const ImageLayer& layer) {
    const TileImage& image = layer.image;
    if (!layer.visible || layer.opacity <= 0.0f || !image.texture.isValid() || image.size.x <= 0 || image.size.y <= 0) return;
    const glm::vec2 topLeft = view.layerOrigin(layer.offset, layer.parallax) + glm::vec2(0.0f, grid.pixelSize().y);
    glm::vec2 first = topLeft - glm::vec2(0.0f, image.size.y);  // the bottom-left of the first copy
    glm::ivec2 count(1);
    // Repeating layers tile across the view on their axis.
    for (int axis : {0, 1}) {
      if (!(axis == 0 ? layer.repeatX : layer.repeatY)) continue;
      const float step = image.size[axis];
      first[axis] += std::floor((view.low[axis] - first[axis]) / step) * step;
      count[axis] = static_cast<int>(std::ceil((view.high[axis] - first[axis]) / step)) + 1;
    }
    const glm::vec4 color = layer.tint * glm::vec4(1.0f, 1.0f, 1.0f, layer.opacity);
    for (int y = 0; y < count.y; ++y) {
      for (int x = 0; x < count.x; ++x) {
        drawImage(image, first + glm::vec2(x, y) * image.size, image.size, glm::mat2(1.0f), color, view.z + layer.z);
      }
    }
  }

  // A tile's look, its bottom-left at `corner`, stretched to `size` (else its image's), flipped as `gid` says.
  void drawTile(const Tile* look, uint32_t gid, glm::vec2 corner, std::optional<glm::vec2> size, glm::vec4 color,
                float z) {
    if (!look || !look->image.texture.isValid()) return;
    // Tiled flips the anti-diagonal first, then horizontally, then vertically (y up here, so the diagonal is x = -y).
    glm::mat2 flip(1.0f);
    if (gid & gid::FlipD) flip = glm::mat2(0, -1, -1, 0);
    if (gid & gid::FlipH) flip = glm::mat2(-1, 0, 0, 1) * flip;
    if (gid & gid::FlipV) flip = glm::mat2(1, 0, 0, -1) * flip;
    drawImage(look->image, corner, size.value_or(look->image.size), flip, color, z);
  }

  void drawImage(const TileImage& image, glm::vec2 corner, glm::vec2 size, const glm::mat2& flip, glm::vec4 color,
                 float z) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(corner + size * 0.5f, z));
    m = m * glm::mat4(glm::vec4(flip[0], 0, 0), glm::vec4(flip[1], 0, 0), glm::vec4(0, 0, 1, 0), glm::vec4(0, 0, 0, 1));
    m = glm::scale(m, glm::vec3(size * 0.5f, 1.0f));
    _renderer.drawSprite(m, color, image.texRect, image.texture, z);
  }
};

// The JSON objects a script sees for a map's objects: world units, bottom-left x/y.
nlohmann::json objectsJson(const TileGrid& grid, glm::vec2 origin) {
  nlohmann::json out = nlohmann::json::array();
  for (const MapObject& o : grid.objects()) {
    const glm::vec2 p = origin + o.position;
    out.push_back({{"id", o.id}, {"name", o.name}, {"type", o.type}, {"layer", o.layer}, {"x", p.x}, {"y", p.y},
                   {"width", o.size.x}, {"height", o.size.y}, {"point", o.point}, {"properties", o.properties}});
  }
  return out;
}

}  // namespace

template <>
struct SystemTraits<TileMapRenderSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = TypeList<TileMapComponent, TransformComponent>;
  using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::Render;
};

void TileMapModule::registerComponents(Engine& app) {
  app.getWorld().registerComponent<TileMapComponent>({
      .fromJson = [this](TileMapComponent& c, const nlohmann::json& json, EntityId) {
        const nlohmann::json source = json.value("map", nlohmann::json());
        const nlohmann::json live = json.value("tilesets", nlohmann::json::object());
        if (source.is_object()) {
          c.grid = load(source, json.value("mapPath", std::string()), live);
        } else if (source.is_string() && !source.get<std::string>().empty()) {
          const std::string path = source.get<std::string>();
          c.grid = load(readJson(path).value_or(nlohmann::json()), path, live);
        }
      },
      .schema = {"Tile Map", "Rendering", "A Tiled map: tile, object and image layers",
                 {FieldSchema::asset("map", {".tmj"}, "The map (.tmj), made here or in Tiled")}},
  });
}

void TileMapModule::initialize(Engine& app) {
  _app = &app;
  _renderer = app.getModules().find<Renderer2DModule>();

  // Maps and tilesets are read on demand (readFile); these only let them sit in an archive.
  app.getAssetManager().addAssetConverter({".tmj", ".tsj"}, [](const RawAsset&, const AssetHandle&) {});
  app.getAssetManager().addAssetTypeConverter("tilemap", [](const RawAsset&, const AssetHandle&) {});
  app.getAssetManager().addAssetTypeConverter("tileset", [](const RawAsset&, const AssetHandle&) {});

  app.getWorld().registerSystem<TileMapRenderSystem>(_renderer->renderer());
  bindScriptApi(app);
  JM_LOG_INFO("[TileMap] initialized");
}

std::optional<nlohmann::json> TileMapModule::readJson(const std::string& path) {
  try {
    const auto bytes = _app->getAssetManager().readFile(path);
    return nlohmann::json::parse(bytes.begin(), bytes.end());
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[TileMap] '{}' can't be read: {}", path, e.what());
    return std::nullopt;
  }
}

std::optional<TileImage> TileMapModule::image(const std::string& path, std::optional<glm::ivec4> rect) {
  auto found = _renderer->resolveImage(path);
  if (!found) return std::nullopt;
  TileImage image{found->texture, found->texRect, found->size};
  if (rect && found->size.x > 0 && found->size.y > 0) {
    // A pixel rectangle (y down) within the image, which may itself be an atlas region.
    const glm::vec2 at = glm::vec2(rect->x, rect->y) / found->size, extent = glm::vec2(rect->z, rect->w) / found->size;
    const glm::vec2 scale(found->texRect.z, found->texRect.w);
    image.texRect = {found->texRect.x + at.x * scale.x, found->texRect.y + at.y * scale.y, extent * scale};
    image.size = glm::vec2(rect->z, rect->w);
  }
  return image;
}

TileGrid TileMapModule::load(const nlohmann::json& map, const std::string& path, const nlohmann::json& liveTilesets) {
  auto resolve = [this](const std::string& file, std::optional<glm::ivec4> rect) { return image(file, rect); };
  auto report = [](const std::string& message) { JM_LOG_ERROR("[TileMap] {}", message); };
  auto loadTileset = [&](const std::string& file) -> std::shared_ptr<const Tileset> {
    if (auto live = liveTilesets.find(file); live != liveTilesets.end()) {
      return std::make_shared<const Tileset>(Tileset::parse(*live, file, resolve, report));
    }
    if (auto it = _tilesets.find(file); it != _tilesets.end()) return it->second;
    auto json = readJson(file);
    if (!json) return nullptr;
    auto set = std::make_shared<const Tileset>(Tileset::parse(*json, file, resolve, report));
    _tilesets[file] = set;
    return set;
  };
  return TileGrid::parse(map, path, loadTileset, resolve, report);
}

void TileMapModule::bindScriptApi(Engine& app) {
  // Scripts address a map by its entity; positions are world units, tiles
  // count from the map's bottom-left corner (the entity's position).
  ScriptManager& s = app.getScriptManager();
  World& world = app.getWorld();
  struct Found {
    TileGrid* grid;
    glm::vec2 origin;
  };
  auto find = [&world](EntityId id) -> std::optional<Found> {
    auto* map = world.getComponent<TileMapComponent>(id);
    auto* transform = world.getComponent<TransformComponent>(id);
    if (!map || !transform) return std::nullopt;
    return Found{&map->grid, glm::vec2(transform->position)};
  };

  // Writes width, height, tile width, tile height, origin x, origin y.
  s.bind("__jmTileMapInfo", [find](EntityId id, host::WasmBytes out) {
    auto m = find(id);
    if (!m || out.size < sizeof(float) * 6) return false;
    const float info[6] = {static_cast<float>(m->grid->width()), static_cast<float>(m->grid->height()),
                           m->grid->tileSize().x, m->grid->tileSize().y, m->origin.x, m->origin.y};
    std::memcpy(out.data, info, sizeof(info));
    return true;
  });
  s.bind("__jmTileMapAt", [find](EntityId id, int32_t tx, int32_t ty) -> std::optional<std::string> {
    auto m = find(id);
    return m ? std::optional(m->grid->at(tx, ty)) : std::nullopt;
  });
  s.bind("__jmTileMapSet", [find](EntityId id, int32_t tx, int32_t ty, std::string type, std::string layer) {
    auto m = find(id);
    return m && m->grid->set(tx, ty, type, layer);
  });
  s.bind("__jmTileMapIs", [find](EntityId id, int32_t tx, int32_t ty, std::string tag) {
    auto m = find(id);
    return m && m->grid->is(tx, ty, tag);
  });
  // [tx, ty, tx, ty, ...] as JSON.
  s.bind("__jmTileMapPositionsOf", [find](EntityId id, std::string type) -> std::optional<std::string> {
    auto m = find(id);
    if (!m) return std::nullopt;
    nlohmann::json out = nlohmann::json::array();
    for (glm::ivec2 p : m->grid->positionsOf(type)) out.push_back(p.x), out.push_back(p.y);
    return out.dump();
  });
  s.bind("__jmTileMapObjects", [find](EntityId id) -> std::optional<std::string> {
    auto m = find(id);
    return m ? std::optional(objectsJson(*m->grid, m->origin).dump()) : std::nullopt;
  });
  s.bind("__jmTileMapProperties", [find](EntityId id) -> std::optional<std::string> {
    auto m = find(id);
    return m ? std::optional(m->grid->properties().dump()) : std::nullopt;
  });
  s.bind("__jmTileMapShowLayer", [find](EntityId id, std::string layer, bool visible) {
    auto m = find(id);
    return m && m->grid->showLayer(layer, visible);
  });
  s.bind("__jmTileMapLoad", [this, find](EntityId id, std::string path) {
    auto m = find(id);
    auto json = m ? readJson(path) : std::nullopt;
    if (json) *m->grid = load(*json, path, nlohmann::json::object());
    return json.has_value();
  });
  // Moves a box; writes x, y, hit x, hit y, hit tile x, hit tile y.
  s.bind("__jmTileMapMove", [find](EntityId id, float x, float y, float halfW, float halfH, float dx, float dy,
                                   float slide, host::WasmBytes out) {
    if (out.size < sizeof(float) * 6) return;
    float result[6] = {x + dx, y + dy, 0, 0, -1, -1};
    if (auto m = find(id)) {
      const TileGrid::Move move = m->grid->move(glm::vec2(x, y) - m->origin, {halfW, halfH}, {dx, dy}, slide);
      const glm::vec2 p = move.position + m->origin;
      result[0] = p.x;
      result[1] = p.y;
      result[2] = static_cast<float>(move.hit.x);
      result[3] = static_cast<float>(move.hit.y);
      result[4] = static_cast<float>(move.hitTile.x);
      result[5] = static_cast<float>(move.hitTile.y);
    }
    std::memcpy(out.data, result, sizeof(result));
  });
}
