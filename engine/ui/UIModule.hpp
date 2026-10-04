#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>

#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetRegistry.hpp"
#include "../renderer2d/TextureHandle.hpp"
#include "FontRegistry.hpp"
#include "GlyphCache.hpp"
#include "UIDocument.hpp"

class AssetManager;
class Renderer2DModule;

// HTML/CSS user interfaces. A scene shows a UI screen by giving an entity a
// UIDocumentComponent { "src": "assets/ui/hud.ui.html", "order": 10 }; the
// document lives exactly as long as the entity. Documents are laid out in
// the renderer's logical resolution and drawn in its screen pass, above the
// world, in ascending `order`. Scripts change them through element ids (see
// UI in @jm/runtime).
class UIModule : public EngineModule {
 public:
  ~UIModule() override = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "UIModule"; }

  FontRegistry& getFontRegistry() { return _fonts; }

  // Applies `fn` to every live document; returns true if any call did.
  template <typename Fn>
  bool forEachDocument(Fn&& fn) {
    bool any = false;
    for (auto& [id, doc] : _documents) any = fn(doc.document) || any;
    return any;
  }

  uint32_t createDocument(AssetHandle templateHandle, int order);
  void destroyDocument(uint32_t id);

 private:
  struct LiveDocument {
    UIDocument document;
    int order;
  };
  class Metrics;

  void paint(Renderer2D& renderer);
  void paintBox(Renderer2D& renderer, const LayoutBox& box, float opacity);
  void paintText(Renderer2D& renderer, const TextPiece& piece, float opacity);

  struct ResolvedFont {
    const Font* font = nullptr;
    FontHandle handle;
  };
  ResolvedFont font(const ComputedStyle& style);
  struct ResolvedImage {
    TextureHandle texture;
    glm::vec4 uv{0, 0, 1, 1};
    glm::vec2 size{0.0f};  // pixels
  };
  const ResolvedImage& image(const std::string& src);
  float textWidth(const ComputedStyle& style, std::string_view text);

  AssetManager* _assets = nullptr;
  Renderer2DModule* _renderer = nullptr;
  FontRegistry _fonts;
  std::string _defaultFont;
  std::unique_ptr<GlyphCache> _glyphs;
  std::unique_ptr<Metrics> _metrics;
  AssetRegistry<UITemplate> _templates;
  std::map<uint32_t, LiveDocument> _documents;  // id order = creation order
  uint32_t _nextDocumentId = 1;
  std::unordered_map<std::string, ResolvedImage> _images;
  std::unordered_set<std::string> _missingFonts;  // tried once, failed
};
