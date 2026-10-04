#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetRegistry.hpp"
#include "FontRegistry.hpp"
#include "GlyphCache.hpp"
#include "UIDocument.hpp"

class Renderer2DModule;
class Renderer2D;
struct LayoutBox;
struct TextPiece;

// HTML/CSS user interfaces. An entity with a UIDocumentComponent
// { "src": "assets/ui/hud.ui.html", "order": 10 } shows that screen for as long
// as the entity lives. Documents are laid out at the renderer's logical
// resolution and drawn above the world in ascending `order`. Scripts change
// them through element ids (UI in the runtime). Text uses the font named by
// CSS font-family, config.ui.defaultFont, or a built-in pixel font.
class UIModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "UIModule"; }

 private:
  struct LiveDocument {
    UIDocument document;
    int order;
  };
  class Metrics;
  struct ResolvedFont {
    const Font* font = nullptr;
    FontHandle handle;
  };

  Engine* _app = nullptr;
  Renderer2DModule* _renderer = nullptr;
  FontRegistry _fonts;
  std::string _defaultFont;
  std::unique_ptr<GlyphCache> _glyphs;
  std::unique_ptr<Metrics> _metrics;
  AssetRegistry<UITemplate> _templates;
  std::map<uint32_t, LiveDocument> _documents;  // creation order
  uint32_t _nextDocumentId = 1;
  std::unordered_set<std::string> _missingFonts;  // tried once, failed

  void registerAssetTypes(Engine& app);
  void bindScriptApi(Engine& app);
  uint32_t createDocument(const std::string& src, int order);
  // Applies `fn` to every live document; true if any call returned true.
  template <typename Fn>
  bool forEachDocument(Fn&& fn);

  ResolvedFont font(const ComputedStyle& style);
  float textWidth(const ComputedStyle& style, std::string_view text);
  void paint(Renderer2D& renderer);
  void paintBox(Renderer2D& renderer, const LayoutBox& box, float opacity);
  void paintText(Renderer2D& renderer, const TextPiece& piece, float opacity);
};
