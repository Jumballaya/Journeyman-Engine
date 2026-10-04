#include "UIModule.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "../core/assets/RawAsset.hpp"
#include "../core/ecs/component/SimpleComponent.hpp"
#include "../core/logger/logging.hpp"
#include "../renderer2d/Renderer2DModule.hpp"
#include "Font.hpp"
#include "HtmlParser.hpp"
#include "UIHostFunctions.hpp"
#include "Utf8.hpp"

// UI draws through the renderer's screen pass and packs glyphs into its
// atlases, so the renderer must exist first.
template <>
struct ModuleTraits<UIModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag>;
};

REGISTER_MODULE(UIModule)

// Attaches a UI document (a .ui.html asset) to an entity.
struct UIDocumentComponent : Component<UIDocumentComponent> {
  COMPONENT_NAME("UIDocumentComponent");
  uint32_t document = 0;
};

struct PODUIDocumentComponent {
  uint32_t document;
};

namespace {
UIModule* s_module = nullptr;  // for the component's raw-pointer onDestroy hook

void destroyDocumentHook(void* component) {
  auto* c = static_cast<UIDocumentComponent*>(component);
  if (s_module && c->document != 0) s_module->destroyDocument(c->document);
}
}  // namespace

class UIModule::Metrics : public LayoutMetrics {
 public:
  explicit Metrics(UIModule& ui) : _ui(ui) {}
  float textWidth(const ComputedStyle& style, std::string_view text) override { return _ui.textWidth(style, text); }
  glm::vec2 imageSize(const std::string& src) override { return _ui.image(src).size; }

 private:
  UIModule& _ui;
};

void UIModule::initialize(Engine& app) {
  s_module = this;
  _assets = &app.getAssetManager();
  _renderer = GetModuleRegistry().find<Renderer2DModule>();
  if (!_renderer) {
    JM_LOG_ERROR("[UI] Renderer2DModule not found; UI disabled");
    return;
  }
  _glyphs = std::make_unique<GlyphCache>(_renderer->atlases(), app.getAssetManager(), _renderer->renderer());
  _metrics = std::make_unique<Metrics>(*this);

  const auto& config = app.getManifest().config;
  if (config.contains("ui") && config["ui"].is_object()) {
    _defaultFont = config["ui"].value("defaultFont", std::string());
  }

  auto fontDecoder = [this](const RawAsset& asset, const AssetHandle& handle) {
    if (asset.data.empty()) {
      JM_LOG_ERROR("[Font] empty buffer for '{}'", asset.filePath.string());
      return;
    }
    // .ttc collections: stbtt_InitFont(index 0) would read only the first
    // face; multi-face support is deferred, so reject explicitly.
    if (asset.data.size() >= 4 && asset.data[0] == 't' && asset.data[1] == 't' &&
        asset.data[2] == 'c' && asset.data[3] == 'f') {
      JM_LOG_WARN("[Font] '{}' is a TrueType Collection (.ttc); rejected.", asset.filePath.string());
      return;
    }
    std::vector<uint8_t> bytes(asset.data.begin(), asset.data.end());
    auto font = Font::tryLoad(std::move(bytes));
    if (!font) {
      JM_LOG_ERROR("[Font] stbtt_InitFont failed for '{}'", asset.filePath.string());
      return;
    }
    _fonts.registerFont(handle, asset.filePath, std::move(font));
    if (_defaultFont.empty()) _defaultFont = asset.filePath.lexically_normal().generic_string();
  };
  app.getAssetManager().addAssetConverter({".ttf", ".otf"}, fontDecoder);
  app.getAssetManager().addAssetTypeConverter("font", fontDecoder);

  // Stylesheets are pulled in by the documents that <link> them.
  app.getAssetManager().addAssetConverter({".css"}, [](const RawAsset&, const AssetHandle&) {});
  app.getAssetManager().addAssetTypeConverter("stylesheet", [](const RawAsset&, const AssetHandle&) {});

  auto uiDecoder = [this](const RawAsset& asset, const AssetHandle& handle) {
    std::string_view html(reinterpret_cast<const char*>(asset.data.data()), asset.data.size());
    ParsedHtml parsed = parseHtml(html);
    auto sheet = std::make_shared<Stylesheet>();
    // Linked stylesheets first, so the document's own <style> wins ties.
    std::vector<const UINode*> stack{parsed.root.get()};
    while (!stack.empty()) {
      const UINode* n = stack.back();
      stack.pop_back();
      if (n->tag == "link") {
        auto rel = n->attributes.find("rel");
        auto href = n->attributes.find("href");
        if (rel != n->attributes.end() && rel->second == "stylesheet" && href != n->attributes.end()) {
          try {
            const RawAsset& css = _assets->getRawAsset(_assets->loadAsset(href->second));
            sheet->append(std::string_view(reinterpret_cast<const char*>(css.data.data()), css.data.size()));
          } catch (const std::exception& e) {
            JM_LOG_ERROR("[UI] {}: stylesheet '{}' failed to load: {}", asset.filePath.string(), href->second, e.what());
          }
        }
      }
      for (auto& c : n->children) stack.push_back(c.get());
    }
    sheet->append(parsed.css);
    _templates.insert(handle, UITemplate{std::shared_ptr<const UINode>(std::move(parsed.root)), sheet});
  };
  app.getAssetManager().addAssetConverter({".ui.html"}, uiDecoder);
  app.getAssetManager().addAssetTypeConverter("ui", uiDecoder);

  registerSimpleComponent<UIDocumentComponent, PODUIDocumentComponent>(
      app.getWorld(),
      [this](UIDocumentComponent& c, const nlohmann::json& j) {
        const std::string src = j.value("src", std::string());
        try {
          c.document = createDocument(_assets->loadAsset(src), j.value("order", 0));
        } catch (const std::exception& e) {
          JM_LOG_ERROR("[UI] document '{}' failed to load: {}", src, e.what());
        }
      },
      [](const UIDocumentComponent&, nlohmann::json&) {},
      [](UIDocumentComponent&, const PODUIDocumentComponent&) {},
      [](const UIDocumentComponent& c) { return PODUIDocumentComponent{c.document}; },
      &destroyDocumentHook);

  setUIHostContext(this);
  registerUIHostFunctions(app.getScriptManager());
  _renderer->addOverlayPass([this](Renderer2D& renderer) { paint(renderer); });
  JM_LOG_INFO("[UI] initialized");
}

void UIModule::shutdown(Engine&) {
  setUIHostContext(nullptr);
  _documents.clear();
  s_module = nullptr;
  JM_LOG_INFO("[UI] shutdown");
}

uint32_t UIModule::createDocument(AssetHandle templateHandle, int order) {
  const UITemplate* tmpl = _templates.get(templateHandle);
  if (!tmpl) {
    JM_LOG_ERROR("[UI] asset is not a UI document (expected .ui.html)");
    return 0;
  }
  const uint32_t id = _nextDocumentId++;
  _documents.emplace(id, LiveDocument{UIDocument(*tmpl), order});
  return id;
}

void UIModule::destroyDocument(uint32_t id) { _documents.erase(id); }

UIModule::ResolvedFont UIModule::font(const ComputedStyle& style) {
  const std::string& path = style.fontFamily.empty() ? _defaultFont : style.fontFamily;
  FontHandle handle = _fonts.handleForPath(path);
  if (!handle.isValid() && !path.empty()) {
    try {
      _assets->loadAsset(path);  // converter registers it
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[UI] font '{}' failed to load: {}", path, e.what());
    }
    handle = _fonts.handleForPath(path);
    if (!handle.isValid()) handle = _fonts.handleForPath(_defaultFont);
  }
  return {_fonts.getFont(handle), handle};
}

float UIModule::textWidth(const ComputedStyle& style, std::string_view text) {
  ResolvedFont f = font(style);
  if (!f.font) return 0.0f;
  const float scale = f.font->scaleFor(style.fontSize);
  float width = 0.0f;
  uint32_t prev = 0;
  for (size_t i = 0; i < text.size();) {
    const uint32_t cp = nextCodepoint(text, i);
    if (prev) width += f.font->kernUnits(prev, cp) * scale;
    width += f.font->advanceUnits(cp) * scale + style.letterSpacing;
    prev = cp;
  }
  return width;
}

const UIModule::ResolvedImage& UIModule::image(const std::string& src) {
  if (auto it = _images.find(src); it != _images.end()) return it->second;
  ResolvedImage img;
  Renderer2D& r = _renderer->renderer();
  try {
    const size_t hash = src.find('#');
    if (hash != std::string::npos) {
      const std::string atlas = src.substr(0, hash);
      _assets->loadAsset(atlas);
      if (auto found = _renderer->atlases().lookupByPath(atlas, src.substr(hash + 1))) {
        img.texture = found->first;
        img.uv = found->second;
      }
    } else if (!src.empty()) {
      img.texture = _renderer->textureFor(_assets->loadAsset(src));
    }
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[UI] image '{}' failed to load: {}", src, e.what());
  }
  if (img.texture.isValid()) {
    img.size = r.textureSize(img.texture) * glm::vec2(img.uv.z, img.uv.w);
  } else if (!src.empty()) {
    JM_LOG_WARN("[UI] image '{}' not found", src);
  }
  return _images.emplace(src, img).first->second;
}

void UIModule::paint(Renderer2D& renderer) {
  if (_documents.empty()) return;
  std::vector<LiveDocument*> ordered;
  for (auto& [id, doc] : _documents) ordered.push_back(&doc);
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const LiveDocument* a, const LiveDocument* b) { return a->order < b->order; });
  const glm::vec2 viewport(renderer.logicalSize());
  for (LiveDocument* doc : ordered) {
    paintBox(renderer, doc->document.layout(viewport, *_metrics), 1.0f);
  }
}

void UIModule::paintBox(Renderer2D& renderer, const LayoutBox& box, float parentOpacity) {
  const ComputedStyle& s = box.style;
  const float opacity = parentOpacity * s.opacity;
  if (opacity <= 0.0f) return;
  const TextureHandle white = renderer.getDefaultTexture();
  const glm::vec4 fullUv(0, 0, 1, 1);
  const glm::vec4 r = box.rect;

  if (s.visible) {
    if (s.backgroundColor.a > 0.0f) {
      renderer.drawScreenQuad(r, s.backgroundColor * glm::vec4(1, 1, 1, opacity), fullUv, white);
    }
    if (!s.backgroundImage.empty()) {
      const ResolvedImage& img = image(s.backgroundImage);
      if (img.texture.isValid()) renderer.drawScreenQuad(r, glm::vec4(1, 1, 1, opacity), img.uv, img.texture);
    }
    if (box.node->tag == "img") {
      auto src = box.node->attributes.find("src");
      if (src != box.node->attributes.end()) {
        const ResolvedImage& img = image(src->second);
        const glm::vec4 content(r.x + s.borderWidth[3] + s.padding[3], r.y + s.borderWidth[0] + s.padding[0],
                                r.z - s.borderWidth[1] - s.borderWidth[3] - s.padding[1] - s.padding[3],
                                r.w - s.borderWidth[0] - s.borderWidth[2] - s.padding[0] - s.padding[2]);
        if (img.texture.isValid()) {
          renderer.drawScreenQuad(content, s.color.a < 1.0f ? glm::vec4(1, 1, 1, opacity * s.color.a)
                                                              : glm::vec4(1, 1, 1, opacity),
                                  img.uv, img.texture);
        }
      }
    }
    if (s.borderColor.a > 0.0f) {
      const glm::vec4 c = s.borderColor * glm::vec4(1, 1, 1, opacity);
      const auto& bw = s.borderWidth;
      if (bw[0] > 0) renderer.drawScreenQuad({r.x, r.y, r.z, bw[0]}, c, fullUv, white);
      if (bw[2] > 0) renderer.drawScreenQuad({r.x, r.y + r.w - bw[2], r.z, bw[2]}, c, fullUv, white);
      if (bw[3] > 0) renderer.drawScreenQuad({r.x, r.y + bw[0], bw[3], r.w - bw[0] - bw[2]}, c, fullUv, white);
      if (bw[1] > 0) renderer.drawScreenQuad({r.x + r.z - bw[1], r.y + bw[0], bw[1], r.w - bw[0] - bw[2]}, c, fullUv, white);
    }
  }
  for (const auto& piece : box.text) {
    if (piece.style->visible) paintText(renderer, piece, opacity);
  }

  std::vector<const LayoutBox*> children;
  for (const auto& c : box.children) children.push_back(c.get());
  std::stable_sort(children.begin(), children.end(),
                   [](const LayoutBox* a, const LayoutBox* b) { return a->style.zIndex < b->style.zIndex; });
  for (const LayoutBox* c : children) paintBox(renderer, *c, opacity);
}

void UIModule::paintText(Renderer2D& renderer, const TextPiece& piece, float opacity) {
  const ComputedStyle& s = *piece.style;
  ResolvedFont f = font(s);
  if (!f.font) return;
  const float pixelScale = std::max(renderer.pixelScale(), 0.01f);
  const uint32_t rasterPx = static_cast<uint32_t>(std::max(1.0f, std::round(s.fontSize * pixelScale)));
  const float logicalScale = f.font->scaleFor(s.fontSize);
  const float ascent = f.font->metrics().ascent * logicalScale;
  const float descent = f.font->metrics().descent * logicalScale;  // negative
  const float baseline = piece.lineTop + (piece.lineHeight - (ascent - descent)) * 0.5f + ascent;
  auto snap = [&](float v) { return std::round(v * pixelScale) / pixelScale; };

  auto drawRun = [&](glm::vec2 offset, glm::vec4 color) {
    float pen = piece.x + offset.x;
    uint32_t prev = 0;
    for (size_t i = 0; i < piece.text.size();) {
      const uint32_t cp = nextCodepoint(piece.text, i);
      if (prev) pen += f.font->kernUnits(prev, cp) * logicalScale;
      const GlyphCache::Glyph& g = _glyphs->get(*f.font, f.handle, rasterPx, s.crispText, cp);
      if (g.texture.isValid()) {
        const glm::vec4 rect(snap(pen + g.offset.x / pixelScale), snap(baseline + offset.y + g.offset.y / pixelScale),
                             g.size.x / pixelScale, g.size.y / pixelScale);
        renderer.drawScreenQuad(rect, color, g.uv, g.texture);
      }
      pen += f.font->advanceUnits(cp) * logicalScale + s.letterSpacing;
      prev = cp;
    }
  };
  if (s.textShadowColor.a > 0.0f) {
    drawRun(s.textShadowOffset, s.textShadowColor * glm::vec4(1, 1, 1, opacity));
  }
  drawRun(glm::vec2(0.0f), s.color * glm::vec4(1, 1, 1, opacity));
}
