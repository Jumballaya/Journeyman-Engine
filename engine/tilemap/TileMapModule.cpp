#include "TileMapModule.hpp"

#include <cmath>
#include <cstring>
#include <optional>

#include "../core/app/Engine.hpp"
#include "../core/app/Registration.hpp"
#include "../core/assets/FileSystem.hpp"
#include "../core/logger/logging.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "TileMapComponent.hpp"
#include "TileMapTerrainSystem.hpp"

REGISTER_MODULE(TileMapModule)

namespace {

// The JSON objects a script sees for a map's objects: world units, bottom-left x/y.
nlohmann::json objectsJson(const TileGrid& grid, glm::vec2 origin) {
  nlohmann::json out = nlohmann::json::array();
  for (const MapObject& o : grid.objects()) {
    const glm::vec2 p = origin + o.position;
    nlohmann::json points = nlohmann::json::array();
    for (const glm::vec2 point : o.points) points.push_back({origin.x + point.x, origin.y + point.y});
    out.push_back({{"id", o.id}, {"name", o.name}, {"type", o.type}, {"layer", o.layer}, {"x", p.x}, {"y", p.y},
                   {"width", o.size.x}, {"height", o.size.y}, {"point", o.point}, {"properties", o.properties},
                   {"points", points}, {"closed", o.closed}});
  }
  return out;
}

}  // namespace

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

  // Maps and tilesets are read when a map loads; a changed tileset is read again then.
  app.getAssetManager().addAssetConverter({".tmj", ".tsj"}, [this](const RawAsset& asset, const AssetHandle&) {
    _tilesets.erase(FileSystem::key(asset.filePath));
  }, AssetManager::Reload::RestartScene);
  app.getAssetManager().addAssetTypeConverter("tilemap", [](const RawAsset&, const AssetHandle&) {});
  app.getAssetManager().addAssetTypeConverter("tileset", [](const RawAsset&, const AssetHandle&) {});
  app.getWorld().registerSystem<TileMapTerrainSystem>();  // a map's ground is its entity's terrain
  JM_LOG_INFO("[TileMap] initialized");
}

void TileMapModule::tickMainThread(Engine& app, float) {
  if (!app.simulating()) _stoppedTerrain.update(app.getWorld(), 0.0f);  // the system runs only while it does
}

std::optional<nlohmann::json> TileMapModule::readJson(const std::string& path) {
  try {
    // Through the asset cache: hot reload sees the file change.
    AssetManager& assets = _app->getAssetManager();
    const auto& bytes = assets.getRawAsset(assets.loadAsset(path)).data;
    return nlohmann::json::parse(bytes.begin(), bytes.end());
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[TileMap] '{}' can't be read: {}", path, e.what());
    return std::nullopt;
  }
}

std::optional<TileImage> TileMapModule::image(const std::string& path, std::optional<glm::ivec4> rect) {
  // No renderer (the server build): maps keep their shapes, and no pixels.
  if (!_resolveImage) return TileImage{{}, {0.0f, 0.0f, 1.0f, 1.0f}, rect ? glm::vec2(rect->z, rect->w) : glm::vec2(0.0f)};
  auto found = _resolveImage(path);
  if (!found) return std::nullopt;
  TileImage image = *found;
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
    if (auto it = _tilesets.find(FileSystem::key(file)); it != _tilesets.end()) return it->second;
    auto json = readJson(file);
    if (!json) return nullptr;
    auto set = std::make_shared<const Tileset>(Tileset::parse(*json, file, resolve, report));
    _tilesets[FileSystem::key(file)] = set;
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
