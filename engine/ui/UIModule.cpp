#include "UIModule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "../core/assets/RawAsset.hpp"
#include "../core/logger/logging.hpp"
#include "../renderer2d/Renderer2DModule.hpp"
#include "Font.hpp"
#include "HtmlParser.hpp"
#include "Text.hpp"
#include "../physics2d/TransformComponent.hpp"

// UI draws through the renderer's screen pass and packs glyphs into its
// atlases, so the renderer must exist first.
template <>
struct ModuleTraits<UIModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag>;
};

REGISTER_MODULE(UIModule)

extern const uint8_t jm_default_font_data[];
extern const size_t jm_default_font_size;

namespace {

constexpr const char* kBuiltinFont = "builtin:default-font";

// Attaches a UI document (a .ui.html asset) to an entity.
struct UIDocumentComponent : Component<UIDocumentComponent> {
  COMPONENT_NAME("UIDocumentComponent");
  uint32_t document = 0;
};

// A line of text in the world, centered vertically on the entity and aligned
// on it horizontally; drawn over the sprites, under the UI documents.
// {"text", "size", "color", "font", "align": "left|center|right", "shadow": color, "crisp"}
struct TextComponent : Component<TextComponent> {
  COMPONENT_NAME("TextComponent");
  std::string text;
  ComputedStyle style;  // font, size, color, shadow, crispness
  float align = 0.5f;   // 0 left, 0.5 center, 1 right
};

// Calls fn(codepoint, pen) per glyph of `text`, with kerning and letter
// spacing applied; returns the advance width.
template <typename Fn>
float forEachGlyph(const Font& font, float scale, float letterSpacing, std::string_view text, Fn&& fn) {
  float pen = 0.0f;
  uint32_t prev = 0;
  for (size_t i = 0; i < text.size();) {
    const uint32_t cp = nextCodepoint(text, i);
    if (prev) pen += font.kernUnits(prev, cp) * scale;
    fn(cp, pen);
    pen += font.advanceUnits(cp) * scale + letterSpacing;
    prev = cp;
  }
  return pen;
}

glm::vec4 colorFrom(const nlohmann::json& value, glm::vec4 fallback) {
  if (value.is_string()) return parseColor(value.get<std::string>()).value_or(fallback);
  if (value.is_array() && value.size() == 4) {
    const auto c = value.get<std::array<float, 4>>();
    return {c[0], c[1], c[2], c[3]};
  }
  return fallback;
}

}  // namespace

class UIModule::Metrics : public LayoutMetrics {
 public:
  explicit Metrics(UIModule& ui) : _ui(ui) {}
  float textWidth(const ComputedStyle& style, std::string_view text) override { return _ui.textWidth(style, text); }
  glm::vec2 imageSize(const std::string& src) override {
    auto image = _ui._renderer->resolveImage(src);
    return image ? image->size : glm::vec2(0.0f);
  }

 private:
  UIModule& _ui;
};

void UIModule::registerComponents(Engine& app) {
  app.getWorld().registerComponent<UIDocumentComponent>({
      .fromJson = [this](UIDocumentComponent& c, const nlohmann::json& json, EntityId) {
        c.document = createDocument(json.value("src", std::string()), json.value("order", 0));
      },
      .onDestroy = [this](UIDocumentComponent& c) { _documents.erase(c.document); },
      .schema = {"UI Document", "UI", "An HTML/CSS screen drawn over the game",
                 {FieldSchema::asset("src", {".ui.html"}, "The document"),
                  FieldSchema::integer("order", 0, "Higher draws on top")}},
  });
  app.getWorld().registerComponent<TextComponent>({
      .fromJson = [](TextComponent& c, const nlohmann::json& json, EntityId) {
        c.text = json.value("text", std::string());
        c.style.fontSize = json.value("size", 8.0f);
        c.style.fontFamily = json.value("font", std::string());
        c.style.color = colorFrom(json.value("color", nlohmann::json()), c.style.color);
        c.style.crispText = json.value("crisp", true);
        if (json.contains("shadow")) {
          c.style.textShadowColor = colorFrom(json["shadow"], glm::vec4(0, 0, 0, 1));
          c.style.textShadowOffset = {1.0f, 1.0f};
        }
        const std::string align = json.value("align", std::string("center"));
        c.align = align == "left" ? 0.0f : align == "right" ? 1.0f : 0.5f;
      },
      .scriptFields = {
          scriptField<TextComponent>("size", [](TextComponent& c) -> float& { return c.style.fontSize; }),
          scriptField<TextComponent>("r", [](TextComponent& c) -> float& { return c.style.color.r; }),
          scriptField<TextComponent>("g", [](TextComponent& c) -> float& { return c.style.color.g; }),
          scriptField<TextComponent>("b", [](TextComponent& c) -> float& { return c.style.color.b; }),
          scriptField<TextComponent>("a", [](TextComponent& c) -> float& { return c.style.color.a; }),
      },
      .schema = {"Text", "UI", "Text drawn in the world at the transform",
                 {FieldSchema::text("text", "", "", true),
                  FieldSchema::number("size", 8, "Font size in pixels", 1, 128, 0.5f),
                  FieldSchema::asset("font", {".ttf", ".otf"}, "Empty = the project's default font"),
                  FieldSchema::color("color", {1, 1, 1, 1}),
                  FieldSchema::choice("align", {"center", "left", "right"}),
                  FieldSchema::boolean("crisp", true, "Snap to whole pixels (pixel fonts)"),
                  FieldSchema::color("shadow", nullptr, "A 1 px drop shadow in this color")}},
  });
}

void UIModule::initialize(Engine& app) {
  _app = &app;
  _renderer = app.getModules().find<Renderer2DModule>();
  _glyphs = std::make_unique<GlyphCache>(_renderer->atlases(), app.getAssetManager(), _renderer->renderer().resources());
  _metrics = std::make_unique<Metrics>(*this);

  const auto& config = app.getManifest().config;
  if (config.contains("ui")) _defaultFont = config["ui"].value("defaultFont", std::string());
  if (auto builtin = Font::tryLoad(std::vector<uint8_t>(jm_default_font_data, jm_default_font_data + jm_default_font_size))) {
    _fonts.registerFont(kBuiltinFont, std::move(builtin));
  }

  registerAssetTypes(app);
  _renderer->addOverlayPass([this](Renderer2D& renderer) { paint(renderer); });
  JM_LOG_INFO("[UI] initialized");
}

void UIModule::registerAssetTypes(Engine& app) {
  AssetManager& assets = app.getAssetManager();

  auto decodeFont = [this](const RawAsset& asset, const AssetHandle&) {
    // .ttc collections would silently load only their first face.
    if (asset.data.size() >= 4 && std::equal(asset.data.begin(), asset.data.begin() + 4, "ttcf")) {
      JM_LOG_ERROR("[UI] '{}' is a font collection (.ttc); use a .ttf/.otf", asset.filePath.string());
      return;
    }
    auto font = Font::tryLoad(std::vector<uint8_t>(asset.data.begin(), asset.data.end()));
    if (!font) {
      JM_LOG_ERROR("[UI] '{}' is not a readable font", asset.filePath.string());
      return;
    }
    _fonts.registerFont(asset.filePath, std::move(font));
  };
  assets.addAssetConverter({".ttf", ".otf"}, decodeFont);
  assets.addAssetTypeConverter("font", decodeFont);

  // Stylesheets are read by the documents that <link> them.
  assets.addAssetConverter({".css"}, [](const RawAsset&, const AssetHandle&) {});
  assets.addAssetTypeConverter("stylesheet", [](const RawAsset&, const AssetHandle&) {});

  auto decodeDocument = [this](const RawAsset& asset, const AssetHandle& handle) {
    const std::string_view html(reinterpret_cast<const char*>(asset.data.data()), asset.data.size());
    _templates.insert(handle, buildTemplate(html, asset.filePath.string()));
  };
  assets.addAssetConverter({".ui.html"}, decodeDocument);
  assets.addAssetTypeConverter("ui", decodeDocument);
}

UITemplate UIModule::buildTemplate(std::string_view html, const std::string& name) {
  AssetManager& assets = _app->getAssetManager();
  ParsedHtml parsed = parseHtml(html);
  auto sheet = std::make_shared<Stylesheet>();
  // Linked sheets first, so the document's own <style> wins ties.
  std::vector<const UINode*> stack{parsed.root.get()};
  while (!stack.empty()) {
    const UINode* n = stack.back();
    stack.pop_back();
    for (auto& child : n->children) stack.push_back(child.get());
    if (n->tag != "link" || !n->attributes.contains("href")) continue;
    const std::string& href = n->attributes.at("href");
    try {
      const RawAsset& css = assets.getRawAsset(assets.loadAsset(href));
      sheet->append(std::string_view(reinterpret_cast<const char*>(css.data.data()), css.data.size()));
    } catch (const std::exception& e) {
      JM_REPORT_ERROR((ErrorSource{href}), "[UI] {}: stylesheet '{}' failed to load: {}", name, href, e.what());
    }
  }
  sheet->append(parsed.css);
  return UITemplate{std::shared_ptr<const UINode>(std::move(parsed.root)), sheet};
}

uint32_t UIModule::openDocument(std::string_view html, int order) {
  return addDocument(buildTemplate(html, "(editor)"), order);
}

uint32_t UIModule::addDocument(const UITemplate& tmpl, int order) {
  const uint32_t id = _nextDocumentId++;
  _documents.emplace(id, LiveDocument{UIDocument(tmpl), order});
  return id;
}

void UIModule::replaceDocument(uint32_t id, std::string_view html) {
  auto it = _documents.find(id);
  if (it != _documents.end()) it->second.document = UIDocument(buildTemplate(html, "(editor)"));
}

const LayoutBox* UIModule::layoutOf(uint32_t id) {
  auto it = _documents.find(id);
  const auto placement = _renderer ? _renderer->uiPlacement() : std::nullopt;
  if (it == _documents.end() || !placement) return nullptr;
  return &it->second.document.layout(placement->layoutSize, *_metrics);
}

namespace {

// A laid-out box as data: elements with their tag, id, classes, rect and text;
// anonymous boxes (text inside a flex container) as just their text.
// `owner` is the nearest box holding text: an inline element (a <span>) has
// none of its own, its words are on that block's lines, so they're found
// there by their style (each piece points at its element's).
nlohmann::json boxJson(const LayoutBox& box, const LayoutBox* owner = nullptr) {
  nlohmann::json out = nlohmann::json::object();
  if (box.node && !box.node->isText()) {
    out["tag"] = box.node->tag;
    if (!box.node->id.empty()) out["id"] = box.node->id;
    if (!box.node->classes.empty()) out["class"] = box.node->classes;
  }
  auto round = [](float v) { return std::round(v * 10.0f) / 10.0f; };
  glm::vec4 rect = box.rect;
  std::string text;
  for (const TextPiece& piece : box.text) text += (text.empty() ? "" : " ") + piece.text;
  if (box.text.empty() && owner) {
    glm::vec2 lo(INFINITY), hi(-INFINITY);
    for (const TextPiece& piece : owner->text) {
      if (piece.style != &box.style) continue;
      text += (text.empty() ? "" : " ") + piece.text;
      lo = glm::min(lo, glm::vec2(piece.x, piece.lineTop));
      hi = glm::max(hi, glm::vec2(piece.x + piece.width, piece.lineTop + piece.lineHeight));
    }
    if (!text.empty() && rect.z == 0.0f && rect.w == 0.0f) rect = {lo, hi - lo};  // where its words are
  }
  out["rect"] = {round(rect.x), round(rect.y), round(rect.z), round(rect.w)};
  // As read: pieces carry their own spacing, so runs of spaces become one.
  text.erase(std::unique(text.begin(), text.end(), [](char a, char b) { return a == ' ' && b == ' '; }), text.end());
  if (!text.empty()) out["text"] = text;
  if (box.style.opacity < 1.0f) out["opacity"] = box.style.opacity;
  if (!box.children.empty()) {
    const LayoutBox* childOwner = box.text.empty() ? owner : &box;
    out["children"] = nlohmann::json::array();
    for (const auto& child : box.children) out["children"].push_back(boxJson(*child, childOwner));
  }
  return out;
}

}  // namespace

void UIModule::describeState(Engine& app, nlohmann::json& state) {
  nlohmann::json documents = nlohmann::json::array();
  for (auto [entity, c] : app.getWorld().view<UIDocumentComponent>()) {
    if (const LayoutBox* root = layoutOf(c->document)) {
      documents.push_back({{"entity", {entity.index, entity.generation}}, {"root", boxJson(*root)}});
    }
  }
  if (!documents.empty()) state["ui"] = std::move(documents);
}

const LayoutBox* UIModule::layoutOfEntity(EntityId entity) {
  const auto* c = _app->getWorld().getComponent<UIDocumentComponent>(entity);
  return c ? layoutOf(c->document) : nullptr;
}

void UIModule::bindScriptApi(Engine& app) {
  // Element ids are matched across every live document, so scripts never
  // need a document handle. Each call returns true if some element matched.
  auto anyDocument = [this](auto&& fn) {
    bool any = false;
    for (auto& [id, doc] : _documents) any = fn(doc.document) || any;
    return any;
  };
  ScriptManager& s = app.getScriptManager();
  s.bind("__jmUISetText", [anyDocument](std::string id, std::string text) {
    return anyDocument([&](UIDocument& d) { return d.setText(id, text); });
  });
  s.bind("__jmUISetClass", [anyDocument](std::string id, std::string cls, bool on) {
    return anyDocument([&](UIDocument& d) { return d.setClass(id, cls, on); });
  });
  s.bind("__jmUISetStyle", [anyDocument](std::string id, std::string property, std::string value) {
    return anyDocument([&](UIDocument& d) { return d.setStyle(id, property, value); });
  });
  s.bind("__jmUISetAttribute", [anyDocument](std::string id, std::string name, std::string value) {
    return anyDocument([&](UIDocument& d) { return d.setAttribute(id, name, value); });
  });
  s.bind("__jmUIExists", [anyDocument](std::string id) {
    return anyDocument([&](UIDocument& d) { return d.has(id); });
  });
  // Writes (x, y, w, h) from the last layout; false if no element has the id.
  s.bind("__jmUIRect", [this](std::string id, host::WasmBytes out) {
    if (out.size < sizeof(float) * 4) return false;
    for (auto& [_, doc] : _documents) {
      if (auto rect = doc.document.rectOf(id)) {
        std::memcpy(out.data, &(*rect)[0], sizeof(float) * 4);
        return true;
      }
    }
    return false;
  });
  s.bind("__jmTextSet", [&app](EntityId entity, std::string text) {
    auto set = [&app, entity, text]() {
      if (auto* c = app.getWorld().getComponent<TextComponent>(entity)) c->text = text;
    };
    if (app.getWorld().getComponent<TextComponent>(entity)) set();
    else app.getSpawner().whenSpawned(entity, set);  // spawned this frame
  });
}

void UIModule::shutdown(Engine&) {
  _documents.clear();
  JM_LOG_INFO("[UI] shutdown");
}

uint32_t UIModule::createDocument(const std::string& src, int order) {
  if (src.empty()) return 0;  // no document chosen yet
  const UITemplate* tmpl = nullptr;
  try {
    tmpl = _templates.get(_app->getAssetManager().loadAsset(src));
  } catch (const std::exception& e) {
    JM_REPORT_ERROR((ErrorSource{src}), "[UI] document '{}' failed to load: {}", src, e.what());
  }
  if (!tmpl) {
    JM_REPORT_ERROR((ErrorSource{src}), "[UI] '{}' is not a UI document (.ui.html)", src);
    return 0;
  }
  return addDocument(*tmpl, order);
}

UIModule::ResolvedFont UIModule::font(const ComputedStyle& style) {
  const std::string& path = style.fontFamily.empty() ? _defaultFont : style.fontFamily;
  FontHandle handle = _fonts.handleForPath(path);
  if (!handle.isValid() && !path.empty() && !_missingFonts.contains(path)) {
    try {
      _app->getAssetManager().loadAsset(path);  // the converter registers it
    } catch (const std::exception& e) {
      JM_REPORT_ERROR((ErrorSource{path}), "[UI] font '{}' failed to load: {}", path, e.what());
    }
    handle = _fonts.handleForPath(path);
    if (!handle.isValid()) _missingFonts.insert(path);
  }
  if (!handle.isValid()) handle = _fonts.handleForPath(_defaultFont);
  if (!handle.isValid()) handle = _fonts.handleForPath(kBuiltinFont);
  return {_fonts.getFont(handle), handle};
}

float UIModule::textWidth(const ComputedStyle& style, std::string_view text) {
  ResolvedFont f = font(style);
  if (!f.font) return 0.0f;
  return forEachGlyph(*f.font, f.font->scaleFor(style.fontSize), style.letterSpacing, text, [](uint32_t, float) {});
}

void UIModule::paintWorldText(Renderer2D& renderer) {
  struct Placed {
    const TextComponent* text;
    glm::vec3 position;
  };
  std::vector<Placed> texts;
  for (auto [entity, text, transform] : _app->getWorld().view<TextComponent, TransformComponent>()) {
    if (!text->text.empty() && text->style.color.a > 0.0f) texts.push_back({text, transform->position});
  }
  std::stable_sort(texts.begin(), texts.end(), [](const Placed& a, const Placed& b) { return a.position.z < b.position.z; });

  const Camera2D& camera = renderer.camera();
  const glm::vec2 half = glm::vec2(renderer.logicalSize()) * 0.5f;
  for (const Placed& placed : texts) {
    const TextComponent& text = *placed.text;
    ComputedStyle style = text.style;
    style.fontSize *= camera.zoom();
    const glm::vec2 offset = (glm::vec2(placed.position) - camera.position()) * camera.zoom();
    TextPiece piece;
    piece.text = text.text;
    piece.style = &style;
    piece.lineHeight = style.fontSize * style.lineHeight;
    piece.x = half.x + offset.x - textWidth(style, text.text) * text.align;
    piece.lineTop = half.y - offset.y - piece.lineHeight * 0.5f;
    paintText(renderer, piece, 1.0f);
  }
}

void UIModule::paint(Renderer2D& renderer) {
  _glyphs->beginFrame();
  paintWorldText(renderer);
  const auto placement = _renderer->uiPlacement();
  if (_documents.empty() || !placement) return;
  std::vector<LiveDocument*> ordered;
  for (auto& [id, doc] : _documents) ordered.push_back(&doc);
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const LiveDocument* a, const LiveDocument* b) { return a->order < b->order; });
  renderer.setScreenTransform(placement->transform);
  for (LiveDocument* doc : ordered) {
    paintBox(renderer, doc->document.layout(placement->layoutSize, *_metrics), 1.0f);
  }
  renderer.setScreenTransform({});
}

void UIModule::paintBox(Renderer2D& renderer, const LayoutBox& box, float parentOpacity) {
  const ComputedStyle& s = box.style;
  const float opacity = parentOpacity * s.opacity;
  if (opacity <= 0.0f) return;
  const glm::vec4 fade(1, 1, 1, opacity);
  const glm::vec4 r = box.rect;
  auto fill = [&](glm::vec4 rect, glm::vec4 color) {
    renderer.drawScreenQuad(rect, color * fade, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  };
  auto image = [&](const std::string& path, glm::vec4 rect) {
    if (auto img = _renderer->resolveImage(path)) renderer.drawScreenQuad(rect, fade, img->texRect, img->texture);
  };

  if (s.visible) {
    const auto& bw = s.borderWidth;
    if (s.backgroundColor.a > 0.0f) fill(r, s.backgroundColor);
    if (!s.backgroundImage.empty()) image(s.backgroundImage, r);
    if (auto src = box.node->attributes.find("src"); box.node->tag == "img" && src != box.node->attributes.end()) {
      const auto& pad = s.padding;
      image(src->second, {r.x + bw[3] + pad[3], r.y + bw[0] + pad[0], r.z - bw[1] - bw[3] - pad[1] - pad[3],
                          r.w - bw[0] - bw[2] - pad[0] - pad[2]});
    }
    if (s.borderColor.a > 0.0f) {
      if (bw[0] > 0) fill({r.x, r.y, r.z, bw[0]}, s.borderColor);
      if (bw[2] > 0) fill({r.x, r.y + r.w - bw[2], r.z, bw[2]}, s.borderColor);
      if (bw[3] > 0) fill({r.x, r.y + bw[0], bw[3], r.w - bw[0] - bw[2]}, s.borderColor);
      if (bw[1] > 0) fill({r.x + r.z - bw[1], r.y + bw[0], bw[1], r.w - bw[0] - bw[2]}, s.borderColor);
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
  const float pixelScale = std::max(renderer.screenPixelScale(), 0.01f);
  const uint32_t rasterPx = static_cast<uint32_t>(std::max(1.0f, std::round(s.fontSize * pixelScale)));
  const float logicalScale = f.font->scaleFor(s.fontSize);
  const float ascent = f.font->metrics().ascent * logicalScale;
  const float descent = f.font->metrics().descent * logicalScale;  // negative
  const float baseline = piece.lineTop + (piece.lineHeight - (ascent - descent)) * 0.5f + ascent;
  auto snap = [&](float v) { return std::round(v * pixelScale) / pixelScale; };

  auto drawRun = [&](glm::vec2 offset, glm::vec4 color) {
    forEachGlyph(*f.font, logicalScale, s.letterSpacing, piece.text, [&](uint32_t cp, float pen) {
      const GlyphCache::Glyph& g = _glyphs->get(*f.font, f.handle, rasterPx, s.crispText, cp);
      if (!g.texture.isValid()) return;
      const glm::vec4 rect(snap(piece.x + offset.x + pen + g.offset.x / pixelScale),
                           snap(baseline + offset.y + g.offset.y / pixelScale), g.size.x / pixelScale,
                           g.size.y / pixelScale);
      renderer.drawScreenQuad(rect, color, g.uv, g.texture);
    });
  };
  if (s.textShadowColor.a > 0.0f) {
    drawRun(s.textShadowOffset, s.textShadowColor * glm::vec4(1, 1, 1, opacity));
  }
  drawRun(glm::vec2(0.0f), s.color * glm::vec4(1, 1, 1, opacity));
}
