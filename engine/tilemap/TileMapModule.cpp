#include "TileMapModule.hpp"

#include <cstring>
#include <sstream>

#include <glm/gtc/matrix_transform.hpp>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "../core/logger/logging.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "../renderer2d/Renderer2DModule.hpp"
#include "TileGrid.hpp"

// Tile images come from the renderer's atlases.
template <>
struct ModuleTraits<TileMapModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag>;
};

REGISTER_MODULE(TileMapModule)

namespace {

// {"tileset": "assets/maps/town.tileset.json" | {...}, "vars": {"theme": "over_"},
//  "rows": ["...", "..."] | "assets/maps/town.txt", "tileSize": 16,
//  "outside": "#" | {"left", "right", "top", "bottom"}}
struct TileMapComponent : Component<TileMapComponent> {
  COMPONENT_NAME("TileMapComponent");
  TileGrid grid;
};

// Draws the tiles in view, each layer at its entity's z.
class TileMapRenderSystem : public System {
 public:
  explicit TileMapRenderSystem(Renderer2D& renderer) : _renderer(renderer) {}

  void update(World& world, float dt) override {
    _time += dt;
    const Camera2D& camera = _renderer.camera();
    const glm::vec2 half = glm::vec2(_renderer.logicalSize()) * 0.5f / camera.zoom();
    for (auto [entity, map, transform] : world.view<TileMapComponent, TransformComponent>()) {
      const TileGrid& grid = map->grid;
      const glm::vec2 origin(transform->position);
      const float z = transform->position.z;
      // Visible tiles, plus a margin for images bigger than their cell.
      const glm::vec2 low = camera.position() - half - origin, high = camera.position() + half - origin;
      const int x0 = std::max(0, grid.tileOf(low.x) - 4), x1 = std::min(grid.width() - 1, grid.tileOf(high.x) + 1);
      const int y0 = std::max(0, grid.tileOf(low.y) - 4), y1 = std::min(grid.height() - 1, grid.tileOf(high.y) + 1);
      for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
          if (const char beneath = grid.under(tx, ty)) draw(grid, origin, tx, ty, beneath, z - 0.001f);
          draw(grid, origin, tx, ty, grid.at(tx, ty), z);
        }
      }
    }
  }

  const char* name() const override { return "TileMapRenderSystem"; }

 private:
  Renderer2D& _renderer;
  float _time = 0.0f;

  // Tile `c` in cell (tx, ty): centered on the cell's bottom edge, or growing
  // up and right from its bottom-left corner.
  void draw(const TileGrid& grid, glm::vec2 origin, int tx, int ty, char c, float z) {
    const TileDef* def = grid.defFor(c);
    if (!def) return;
    const TileImage* image = def->image(def->images.size() > 1 ? grid.mask(tx, ty, c, *def) : 0, _time);
    if (!image) return;
    const float ts = grid.tileSize();
    const glm::vec2 cell = origin + glm::vec2(static_cast<float>(tx), static_cast<float>(ty)) * ts;
    const glm::vec2 center = def->anchorBottomLeft ? cell + image->size * 0.5f
                                                   : cell + glm::vec2(ts * 0.5f, image->size.y * 0.5f);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(center, z));
    m = glm::scale(m, glm::vec3(image->size * 0.5f, 1.0f));
    _renderer.drawSprite(m, glm::vec4(1.0f), image->texRect, image->texture, z);
  }
};

TileGrid::Outside readOutside(const nlohmann::json& json) {
  TileGrid::Outside out;
  auto letter = [](const nlohmann::json& v, char fallback) {
    return v.is_string() && !v.get<std::string>().empty() ? v.get<std::string>()[0] : fallback;
  };
  if (json.is_string()) {
    out.left = out.right = out.top = out.bottom = letter(json, ' ');
  } else if (json.is_object()) {
    out.left = letter(json.value("left", nlohmann::json()), ' ');
    out.right = letter(json.value("right", nlohmann::json()), ' ');
    out.top = letter(json.value("top", nlohmann::json()), ' ');
    out.bottom = letter(json.value("bottom", nlohmann::json()), ' ');
  }
  return out;
}

std::vector<std::string> splitLines(const std::vector<uint8_t>& bytes) {
  std::vector<std::string> lines;
  std::istringstream in(std::string(bytes.begin(), bytes.end()));
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
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

void TileMapModule::initialize(Engine& app) {
  _app = &app;
  _renderer = GetModuleRegistry().find<Renderer2DModule>();

  app.getAssetManager().addAssetConverter({".tileset.json"}, [](const RawAsset&, const AssetHandle&) {});
  app.getAssetManager().addAssetTypeConverter("tileset", [](const RawAsset&, const AssetHandle&) {});

  app.getWorld().registerComponent<TileMapComponent>({
      .fromJson = [this](TileMapComponent& c, const nlohmann::json& json, EntityId) {
        const nlohmann::json vars = json.value("vars", nlohmann::json::object());
        auto set = tileset(json.value("tileset", nlohmann::json()), vars);
        std::vector<std::string> rows;
        const auto& source = json.value("rows", nlohmann::json());
        if (source.is_array()) {
          rows = source.get<std::vector<std::string>>();
        } else if (source.is_string()) {
          try {
            rows = splitLines(_app->getAssetManager().readFile(source.get<std::string>()));
          } catch (const std::exception& e) {
            JM_LOG_ERROR("[TileMap] rows '{}' can't be read: {}", source.get<std::string>(), e.what());
          }
        }
        c.grid = TileGrid(std::move(rows), set, json.value("tileSize", 16.0f),
                          readOutside(json.value("outside", nlohmann::json())));
      },
  });
  app.getWorld().registerSystem<TileMapRenderSystem>(_renderer->renderer());
  bindScriptApi(app);
  JM_LOG_INFO("[TileMap] initialized");
}

std::shared_ptr<const Tileset> TileMapModule::tileset(const nlohmann::json& source, const nlohmann::json& vars) {
  const std::string key = (source.is_string() ? source.get<std::string>() : source.dump()) + "|" + vars.dump();
  if (auto it = _tilesets.find(key); it != _tilesets.end()) return it->second;

  nlohmann::json json = source;
  if (source.is_string()) {
    try {
      const auto bytes = _app->getAssetManager().readFile(source.get<std::string>());
      json = nlohmann::json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[TileMap] tileset '{}' can't be read: {}", source.get<std::string>(), e.what());
      json = nlohmann::json::object();
    }
  }
  auto resolve = [this](const std::string& reference) -> std::optional<TileImage> {
    auto image = _renderer->resolveImage(reference);
    if (!image) return std::nullopt;
    return TileImage{image->texture, image->texRect, image->size};
  };
  auto missing = [](const std::string& reference) { JM_LOG_ERROR("[TileMap] tile image '{}' not found", reference); };
  auto set = std::make_shared<const Tileset>(Tileset::parse(json, vars, resolve, missing));
  _tilesets[key] = set;
  return set;
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

  // Writes width, height, tile size, origin x, origin y.
  s.bind("__jmTileMapInfo", [find](EntityId id, host::WasmBytes out) {
    auto m = find(id);
    if (!m || out.size < sizeof(float) * 5) return false;
    const float info[5] = {static_cast<float>(m->grid->width()), static_cast<float>(m->grid->height()),
                           m->grid->tileSize(), m->origin.x, m->origin.y};
    std::memcpy(out.data, info, sizeof(info));
    return true;
  });
  s.bind("__jmTileMapAt", [find](EntityId id, int32_t tx, int32_t ty) -> int32_t {
    auto m = find(id);
    return m ? static_cast<unsigned char>(m->grid->at(tx, ty)) : -1;
  });
  s.bind("__jmTileMapSet", [find](EntityId id, int32_t tx, int32_t ty, int32_t c) {
    if (auto m = find(id)) m->grid->set(tx, ty, static_cast<char>(c));
  });
  s.bind("__jmTileMapIs", [find](EntityId id, int32_t tx, int32_t ty, std::string tag) {
    auto m = find(id);
    return m && m->grid->is(tx, ty, tag);
  });
  s.bind("__jmTileMapSetRows", [find](EntityId id, std::string rowsJson) {
    auto m = find(id);
    const auto rows = nlohmann::json::parse(rowsJson, nullptr, false);
    if (m && rows.is_array()) m->grid->setRows(rows.get<std::vector<std::string>>());
  });
  s.bind("__jmTileMapLoad", [this, find](EntityId id, std::string path) {
    auto m = find(id);
    if (!m) return false;
    try {
      m->grid->setRows(splitLines(_app->getAssetManager().readFile(path)));
      return true;
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[TileMap] rows '{}' can't be read: {}", path, e.what());
      return false;
    }
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
