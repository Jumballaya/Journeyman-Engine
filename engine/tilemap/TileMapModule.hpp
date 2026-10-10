#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "../core/app/EngineModule.hpp"
#include "TileGrid.hpp"
#include "TileMapTerrainSystem.hpp"

class Engine;

// Tile maps: a TileMapComponent holds a Tiled map (.tmj) at its entity (the
// map's bottom-left corner) and answers scripts' questions about it (what is
// where, what is solid, moving boxes through it, its objects). Format: docs/content.md.
// Drawing them is TileMapRenderModule's (render/), which a server build leaves out.
class TileMapModule : public EngineModule {
 public:
  void registerComponents(Engine& app) override;
  void bindScriptApi(Engine& app) override;
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  void tickMainThread(Engine& app, float) override;
  const char* name() const override { return "TileMapModule"; }

  // How maps find their images (whole images and atlas regions, by path):
  // the renderer's. Without one, images are blank and the maps work the same.
  using ImageResolver = std::function<std::optional<TileImage>(const std::string& path)>;
  void setImageResolver(ImageResolver resolver) { _resolveImage = std::move(resolver); }

 private:
  Engine* _app = nullptr;
  ImageResolver _resolveImage;
  TileMapTerrainSystem _stoppedTerrain;  // maps' ground while nothing simulates (an editor's scene view)
  // Parsed .tsj tilesets by path, shared by the maps using them.
  std::unordered_map<std::string, std::shared_ptr<const Tileset>> _tilesets;

  // `map` (a .tmj's JSON) at project path `path`. `liveTilesets` ({path: json})
  // stand in for those files (the editor's unsaved edits).
  TileGrid load(const nlohmann::json& map, const std::string& path, const nlohmann::json& liveTilesets);
  std::optional<nlohmann::json> readJson(const std::string& path);
  std::optional<TileImage> image(const std::string& path, std::optional<glm::ivec4> rect);
};
