#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "../core/app/EngineModule.hpp"
#include "TileGrid.hpp"

class Engine;
class Renderer2DModule;

// Tile maps: a TileMapComponent draws a Tiled map (.tmj) at its entity (the
// map's bottom-left corner) and answers scripts' questions about it (what is
// where, what is solid, moving boxes through it, its objects). Format: docs/content.md.
class TileMapModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  const char* name() const override { return "TileMapModule"; }

 private:
  Engine* _app = nullptr;
  Renderer2DModule* _renderer = nullptr;
  // Parsed .tsj tilesets by path, shared by the maps using them.
  std::unordered_map<std::string, std::shared_ptr<const Tileset>> _tilesets;

  // `map` (a .tmj's JSON) at project path `path`. `liveTilesets` ({path: json})
  // stand in for those files (the editor's unsaved edits).
  TileGrid load(const nlohmann::json& map, const std::string& path, const nlohmann::json& liveTilesets);
  std::optional<nlohmann::json> readJson(const std::string& path);
  std::optional<TileImage> image(const std::string& path, std::optional<glm::ivec4> rect);
  void bindScriptApi(Engine& app);
};
