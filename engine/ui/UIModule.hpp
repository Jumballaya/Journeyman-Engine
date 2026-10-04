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

// HTML/CSS screens: a UIDocumentComponent { "src", "order" } shows one while its
// entity lives, above the world, at logical resolution. Scripts edit by element id.
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
