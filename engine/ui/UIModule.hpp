#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>

#include "../core/app/EngineModule.hpp"
#include "../core/ecs/entity/EntityId.hpp"
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

  // Authoring, for the editor: a document made from HTML text rather than a
  // file (linked stylesheets still load from the project), re-made whenever
  // the text changes, and its layout at the current UI size (null before the
  // first frame, or when UI isn't shown).
  uint32_t openDocument(std::string_view html, int order = 0);
  void replaceDocument(uint32_t id, std::string_view html);
  void closeDocument(uint32_t id) { _documents.erase(id); }
  const LayoutBox* layoutOf(uint32_t id);
  // The layout of the document an entity's UIDocumentComponent shows, or null.
  const LayoutBox* layoutOfEntity(EntityId entity);

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
  // Parses a document and gathers its stylesheet (linked sheets, then <style>).
  UITemplate buildTemplate(std::string_view html, const std::string& name);
  void bindScriptApi(Engine& app);
  uint32_t createDocument(const std::string& src, int order);  // from a .ui.html asset; 0 if none
  uint32_t addDocument(const UITemplate& tmpl, int order);

  ResolvedFont font(const ComputedStyle& style);
  float textWidth(const ComputedStyle& style, std::string_view text);
  void paint(Renderer2D& renderer);
  void paintWorldText(Renderer2D& renderer);
  void paintBox(Renderer2D& renderer, const LayoutBox& box, float opacity);
  void paintText(Renderer2D& renderer, const TextPiece& piece, float opacity);
};
