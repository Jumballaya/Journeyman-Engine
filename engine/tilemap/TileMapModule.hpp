#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "../core/app/EngineModule.hpp"
#include "Tileset.hpp"

class Engine;
class Renderer2DModule;

// Tile maps: a TileMapComponent draws an ASCII grid of tiles at its entity
// (the grid's bottom-left corner) and answers scripts' questions about it
// (what is where, what is solid, moving boxes through it). Format: docs/content.md.
class TileMapModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  const char* name() const override { return "TileMapModule"; }

 private:
  Engine* _app = nullptr;
  Renderer2DModule* _renderer = nullptr;
  // Parsed tilesets by file path + vars, shared by the maps using them.
  std::unordered_map<std::string, std::shared_ptr<const Tileset>> _tilesets;

  std::shared_ptr<const Tileset> tileset(const nlohmann::json& source, const nlohmann::json& vars);
  void bindScriptApi(Engine& app);
};
