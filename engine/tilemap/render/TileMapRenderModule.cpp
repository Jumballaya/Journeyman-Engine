#include "TileMapRenderModule.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "../../core/app/Engine.hpp"
#include "../../core/app/ModuleTags.hpp"
#include "../../core/app/ModuleTraits.hpp"
#include "../../core/app/Registration.hpp"
#include "../../core/ecs/system/SystemTraits.hpp"
#include "../../core/logger/logging.hpp"
#include "../../physics2d/TransformComponent.hpp"
#include "../../renderer2d/Renderer2DModule.hpp"
#include "../TileMapComponent.hpp"
#include "../TileMapModule.hpp"

// Tile images come from the renderer.
template <>
struct ModuleTraits<TileMapRenderModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag>;
};

REGISTER_MODULE(TileMapRenderModule)

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

  // Shadows fall by world position, so a parallax layer (not drawn where its world position is) gets none.
  static Renderer2D::Lit litFor(glm::vec2 parallax) {
    return parallax == glm::vec2(1.0f) ? Renderer2D::Lit::Yes : Renderer2D::Lit::Unshadowed;
  }

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
        drawTile(tileset->frame(id, _time), gid, corner, std::nullopt, color, view.z + layer.z, litFor(layer.parallax));
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
        drawImage(image, first + glm::vec2(x, y) * image.size, image.size, glm::mat2(1.0f), color, view.z + layer.z,
                  litFor(layer.parallax));
      }
    }
  }

  // A tile's look, its bottom-left at `corner`, stretched to `size` (else its image's), flipped as `gid` says.
  void drawTile(const Tile* look, uint32_t gid, glm::vec2 corner, std::optional<glm::vec2> size, glm::vec4 color,
                float z, Renderer2D::Lit lit = Renderer2D::Lit::Yes) {
    if (!look || !look->image.texture.isValid()) return;
    // Tiled flips the anti-diagonal first, then horizontally, then vertically (y up here, so the diagonal is x = -y).
    glm::mat2 flip(1.0f);
    if (gid & gid::FlipD) flip = glm::mat2(0, -1, -1, 0);
    if (gid & gid::FlipH) flip = glm::mat2(-1, 0, 0, 1) * flip;
    if (gid & gid::FlipV) flip = glm::mat2(1, 0, 0, -1) * flip;
    drawImage(look->image, corner, size.value_or(look->image.size), flip, color, z, lit);
  }

  void drawImage(const TileImage& image, glm::vec2 corner, glm::vec2 size, const glm::mat2& flip, glm::vec4 color,
                 float z, Renderer2D::Lit lit) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(corner + size * 0.5f, z));
    m = m * glm::mat4(glm::vec4(flip[0], 0, 0), glm::vec4(flip[1], 0, 0), glm::vec4(0, 0, 1, 0), glm::vec4(0, 0, 0, 1));
    m = glm::scale(m, glm::vec3(size * 0.5f, 1.0f));
    _renderer.drawSprite(m, color, image.texRect, image.texture, z, lit);
  }
};


}  // namespace

template <>
struct SystemTraits<TileMapRenderSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = TypeList<TileMapComponent, TransformComponent>;
  using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::Render;
};

void TileMapRenderModule::initialize(Engine& app) {
  auto* renderer = app.getModules().find<Renderer2DModule>();
  auto* tilemaps = app.getModules().find<TileMapModule>();
  if (!renderer || !tilemaps) return;
  tilemaps->setImageResolver([renderer](const std::string& path) -> std::optional<TileImage> {
    auto found = renderer->resolveImage(path);
    if (!found) return std::nullopt;
    return TileImage{found->texture, found->texRect, found->size};
  });
  app.getWorld().registerSystem<TileMapRenderSystem>(renderer->renderer());
}
