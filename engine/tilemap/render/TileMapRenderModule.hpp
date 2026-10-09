#pragma once

#include "../../core/app/EngineModule.hpp"

// Draws tile maps (TileMapModule's) with the renderer, and gives their
// tilesets the renderer's images. A server build leaves it out.
class TileMapRenderModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  const char* name() const override { return "TileMapRenderModule"; }
};
