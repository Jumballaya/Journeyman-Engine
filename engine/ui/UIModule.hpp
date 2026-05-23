#pragma once

#include "../core/app/EngineModule.hpp"
#include "FontRegistry.hpp"

class Engine;

// UIModule owns the engine-side UI infrastructure. G.1 ships only the font
// primitive: it registers a TTF/OTF asset converter on initialize and owns the
// FontRegistry the converter populates. G.2+ extend this module (text renderer,
// UI deserializer, etc.) — leave room.
class UIModule : public EngineModule {
 public:
  ~UIModule() = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  const char* name() const override { return "UIModule"; }

  // G.2 reaches the registry directly through the captured `this` in
  // initialize's converter lambda; this accessor exists for symmetry with
  // Renderer2DModule and for future host-context wiring.
  FontRegistry& getFontRegistry() { return _fonts; }

 private:
  // Direct value ownership, mirroring _atlasManager in Renderer2DModule. (Fonts
  // are held inside the registry via unique_ptr — that's about Font's
  // stable-address requirement, not the registry's storage shape here.)
  FontRegistry _fonts;
};
