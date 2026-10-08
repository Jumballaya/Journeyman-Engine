// The UI editor (*.ui.html): the screen drawn by the engine at the game's
// resolution, with its elements outlined; click to select, insert boxes,
// rows, text and images, reorder in the outline, and edit the selected
// element's text, id, classes and style in the Inspector. Every change is a
// small edit to the HTML text, so hand-written files stay as written.

#include <algorithm>
#include <cmath>
#include <set>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "EditorWidgets.hpp"
#include "HostedEngine.hpp"
#include "Icons.hpp"
#include "LogBook.hpp"
#include "Scroll.hpp"
#include "Theme.hpp"
#include "UiSource.hpp"
#include "Ui.hpp"
#include "ui/UIModule.hpp"

namespace {

using uisource::Path;

// A style property the Inspector offers, grouped like a design tool's panel.
struct StyleProperty {
  const char* name;
  const char* label;
  enum Kind { Text, Color, Choice } kind;
  std::vector<const char*> choices;
  const char* hint;
};

struct StyleSection {
  const char* title;
  const char* icon;
  std::vector<StyleProperty> properties;
};

const std::vector<StyleSection>& styleSections() {
  static const std::vector<StyleSection> kSections = {
      {"Layout", ICON_SQUARES_FOUR,
       {{"display", "Display", StyleProperty::Choice, {"block", "flex", "inline", "none"}, "block"},
        {"flex-direction", "Direction", StyleProperty::Choice, {"row", "column"}, "row"},
        {"justify-content", "Justify", StyleProperty::Choice, {"start", "center", "end", "space-between", "space-around", "space-evenly"}, "start"},
        {"align-items", "Align", StyleProperty::Choice, {"stretch", "start", "center", "end"}, "stretch"},
        {"gap", "Gap", StyleProperty::Text, {}, "0px"},
        {"flex-grow", "Grow", StyleProperty::Text, {}, "0"}}},
      {"Position", ICON_ARROWS_OUT_CARDINAL,
       {{"position", "Position", StyleProperty::Choice, {"static", "relative", "absolute"}, "static"},
        {"top", "Top", StyleProperty::Text, {}, "auto"},
        {"right", "Right", StyleProperty::Text, {}, "auto"},
        {"bottom", "Bottom", StyleProperty::Text, {}, "auto"},
        {"left", "Left", StyleProperty::Text, {}, "auto"},
        {"z-index", "Z index", StyleProperty::Text, {}, "0"}}},
      {"Size", ICON_FRAME_CORNERS,
       {{"width", "Width", StyleProperty::Text, {}, "auto"},
        {"height", "Height", StyleProperty::Text, {}, "auto"},
        {"min-width", "Min width", StyleProperty::Text, {}, "none"},
        {"max-width", "Max width", StyleProperty::Text, {}, "none"},
        {"margin", "Margin", StyleProperty::Text, {}, "0 (top right bottom left)"},
        {"padding", "Padding", StyleProperty::Text, {}, "0 (top right bottom left)"}}},
      {"Text", ICON_TEXT_T,
       {{"color", "Color", StyleProperty::Color, {}, "inherited"},
        {"font-size", "Size", StyleProperty::Text, {}, "inherited"},
        {"text-align", "Align", StyleProperty::Choice, {"left", "center", "right"}, "inherited"},
        {"font-family", "Font", StyleProperty::Text, {}, "the UI font (a .ttf path)"},
        {"line-height", "Line height", StyleProperty::Text, {}, "1.25"},
        {"letter-spacing", "Letter spacing", StyleProperty::Text, {}, "0px"},
        {"text-transform", "Transform", StyleProperty::Choice, {"none", "uppercase"}, "none"},
        {"text-shadow", "Shadow", StyleProperty::Text, {}, "2px 2px #000"}}},
      {"Fill", ICON_PAINT_BUCKET,
       {{"background-color", "Background", StyleProperty::Color, {}, "none"},
        {"background-image", "Image", StyleProperty::Text, {}, "url(assets/...)"},
        {"border", "Border", StyleProperty::Text, {}, "2px solid #fff"},
        {"opacity", "Opacity", StyleProperty::Text, {}, "1"},
        {"visibility", "Visibility", StyleProperty::Choice, {"visible", "hidden"}, "visible"}}},
  };
  return kSections;
}

// "#rgb", "#rgba", "#rrggbb", "#rrggbbaa" → RGBA 0..1.
std::optional<std::array<float, 4>> parseHex(const std::string& s) {
  if (!s.starts_with('#')) return std::nullopt;
  std::string h = s.substr(1);
  if (h.size() == 3 || h.size() == 4) {
    std::string wide;
    for (char c : h) wide += std::string(2, c);
    h = wide;
  }
  if (h.size() != 6 && h.size() != 8) return std::nullopt;
  std::array<float, 4> out{0, 0, 0, 1};
  for (size_t i = 0; i < h.size() / 2; ++i) {
    out[i] = static_cast<float>(std::strtoul(h.substr(i * 2, 2).c_str(), nullptr, 16)) / 255.0f;
  }
  return out;
}

std::string toHex(const float c[4]) {
  char out[16];
  auto b = [](float v) { return static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * 255)); };
  if (b(c[3]) == 255) std::snprintf(out, sizeof(out), "#%02x%02x%02x", b(c[0]), b(c[1]), b(c[2]));
  else std::snprintf(out, sizeof(out), "#%02x%02x%02x%02x", b(c[0]), b(c[1]), b(c[2]), b(c[3]));
  return out;
}

// What a property comes to on the element as laid out (stylesheets, inheritance and
// all), shown faintly where nothing inline sets it. "" when there's nothing to say.
std::string computedValue(const LayoutBox& box, const std::string& property) {
  const ComputedStyle& s = box.style;
  auto length = [](const Length& l) -> std::string {
    if (l.unit == Length::Unit::Auto) return "auto";
    char out[24];
    std::snprintf(out, sizeof(out), l.unit == Length::Unit::Percent ? "%g%%" : "%gpx", l.value);
    return out;
  };
  auto number = [](float v, const char* suffix = "") {
    char out[24];
    std::snprintf(out, sizeof(out), "%g%s", v, suffix);
    return std::string(out);
  };
  auto color = [](const glm::vec4& c) {
    const float rgba[4] = {c.r, c.g, c.b, c.a};
    return c.a <= 0.0f ? std::string("none") : toHex(rgba);
  };
  auto sides = [&](auto get) { return get(0) + " " + get(1) + " " + get(2) + " " + get(3); };
  static const char* kDisplay[] = {"block", "inline", "flex", "none"};
  static const char* kJustify[] = {"start", "center", "end", "space-between", "space-around", "space-evenly"};
  static const char* kAlign[] = {"start", "center", "end", "stretch"};
  static const char* kPosition[] = {"static", "relative", "absolute"};
  static const char* kTextAlign[] = {"left", "center", "right"};
  if (property == "display") return kDisplay[static_cast<int>(s.display)];
  if (property == "flex-direction") return s.flexDirection == FlexDirection::Row ? "row" : "column";
  if (property == "justify-content") return kJustify[static_cast<int>(s.justifyContent)];
  if (property == "align-items") return kAlign[static_cast<int>(s.alignItems)];
  if (property == "gap") return number(s.gap, "px");
  if (property == "flex-grow") return number(s.flexGrow);
  if (property == "position") return kPosition[static_cast<int>(s.position)];
  if (property == "top") return length(s.top);
  if (property == "right") return length(s.right);
  if (property == "bottom") return length(s.bottom);
  if (property == "left") return length(s.left);
  if (property == "z-index") return number(static_cast<float>(s.zIndex));
  if (property == "width") return s.width.isAuto() ? "auto (" + number(box.rect.z, "px") + ")" : length(s.width);
  if (property == "height") return s.height.isAuto() ? "auto (" + number(box.rect.w, "px") + ")" : length(s.height);
  if (property == "min-width") return s.minWidth.isAuto() ? "none" : length(s.minWidth);
  if (property == "max-width") return s.maxWidth.isAuto() ? "none" : length(s.maxWidth);
  if (property == "margin") return sides([&](int i) { return length(s.margin[static_cast<size_t>(i)]); });
  if (property == "padding") return sides([&](int i) { return number(s.padding[static_cast<size_t>(i)], "px"); });
  if (property == "color") return color(s.color);
  if (property == "font-size") return number(s.fontSize, "px");
  if (property == "text-align") return kTextAlign[static_cast<int>(s.textAlign)];
  if (property == "font-family") return s.fontFamily.empty() ? "the UI font" : s.fontFamily;
  if (property == "line-height") return number(s.lineHeight);
  if (property == "letter-spacing") return number(s.letterSpacing, "px");
  if (property == "text-transform") return s.uppercase ? "uppercase" : "none";
  if (property == "background-color") return color(s.backgroundColor);
  if (property == "background-image") return s.backgroundImage.empty() ? "none" : "url(" + s.backgroundImage + ")";
  if (property == "border") return s.borderWidth[0] > 0 ? number(s.borderWidth[0], "px solid ") + color(s.borderColor) : "none";
  if (property == "opacity") return number(s.opacity);
  if (property == "visibility") return s.visible ? "visible" : "hidden";
  return "";
}

// "div#hud.panel.big": how an element reads in the outline and labels.
std::string elementLabel(const UINode& n) {
  std::string out = n.tag;
  if (!n.id.empty()) out += "#" + n.id;
  for (const std::string& c : n.classes) out += "." + c;
  return out;
}

const char* elementIcon(const UINode& n) {
  if (n.tag == "img") return ICON_IMAGE;
  if (n.tag == "link") return ICON_LINK;
  if (uisource::elements(n).empty() && !n.children.empty()) return ICON_TEXT_T;
  return ICON_SQUARE;
}

// The element's class attribute set to `classes` (removed when there are none).
std::string withClasses(const std::string& html, const UINode& node, const std::vector<std::string>& classes) {
  std::string list;
  for (const std::string& c : classes) list += (list.empty() ? "" : " ") + c;
  return uisource::setAttribute(html, node, "class", list.empty() ? std::nullopt : std::optional(list));
}

std::string inlineValue(const UINode& node, const std::string& property) {
  for (const auto& [p, v] : uisource::inlineStyle(node)) {
    if (p == property) return v;
  }
  return "";
}

// "12px" / "12" → 12; anything else (auto, %, empty) → nullopt.
std::optional<float> pixels(const std::string& value) {
  if (value.empty()) return std::nullopt;
  char* end = nullptr;
  const float v = std::strtof(value.c_str(), &end);
  const std::string rest = end;
  return end != value.c_str() && (rest.empty() || rest == "px") ? std::optional(v) : std::nullopt;
}

std::string px(float v) { return std::to_string(static_cast<int>(std::round(v))) + "px"; }

// Whether the mouse is on the resize handle at a selection's bottom-right `corner`.
bool onHandle(ImVec2 mouse, ImVec2 corner) { return std::abs(mouse.x - corner.x) <= 7 && std::abs(mouse.y - corner.y) <= 7; }

const LayoutBox* findBox(const LayoutBox& box, const Path& path) {
  if (box.node && !box.node->isText() && uisource::pathOf(*box.node) == path) return &box;
  for (const auto& c : box.children) {
    if (const LayoutBox* found = findBox(*c, path)) return found;
  }
  return nullptr;
}

struct Insert {
  const char* icon;
  const char* label;
  const char* snippet;
};

constexpr Insert kInserts[] = {
    {ICON_SQUARE, "Box", "<div style=\"padding: 8px; background-color: #00000088\"></div>"},
    {ICON_COLUMNS, "Row", "<div style=\"display: flex; gap: 8px; align-items: center\"></div>"},
    {ICON_ROWS, "Column", "<div style=\"display: flex; flex-direction: column; gap: 8px\"></div>"},
    {ICON_TEXT_T, "Text", "<div>Text</div>"},
    {ICON_IMAGE, "Image", "<img src=\"\" style=\"width: 32px; height: 32px\">"},
};

class UiEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;
  void show(const std::string& item) override {
    _selected = uisource::parsePath(item);
    _scrollOutline = true;
  }
  bool handles(const std::string& command) const override {
    return selected() && (command == "edit.duplicate" || command == "edit.delete");
  }
  void run(const std::string& command, AssetDocument& doc) override {
    if (command == "edit.duplicate") duplicate(doc, *selected());
    else erase(doc, *selected());
  }

 private:
  std::unique_ptr<HostedEngine> _engine;
  std::string _engineError;
  uint64_t _engineBuild = ~0ull;
  uint32_t _document = 0;
  uint64_t _shownRevision = ~0ull;
  glm::ivec2 _gameSize{320, 240};
  ParsedHtml _parsed;  // the editor's own parse, with source ranges
  std::string _css;    // every stylesheet the document uses, for class suggestions
  std::optional<Path> _selected, _hovered;
  std::optional<Path> _shownSelection;  // the selection the engine's copy reveals
  bool _scrollOutline = false;          // bring the selection into view in the outline
  float _zoom = 0;  // 0 = fit
  bool _code = false;
  std::string _draft;  // the open popup's text: a new class, or an element's text
  // Where the canvas put the game frame last frame: screen top-left and scale.
  ImVec2 _frameOrigin{};
  float _frameScale = 1;
  // A drag on the canvas: moving an absolutely placed element, or resizing one.
  struct Drag {
    enum class Kind { Move, Resize } kind;
    ImVec2 startMouse;
    glm::vec4 startRect;     // its box when the drag began (game px)
    glm::vec2 startOffset;   // its left/top (or right/bottom) then
    bool fromRight = false, fromBottom = false;
    std::optional<Path> clicked;  // what a click without a move selects
    bool moved = false;
  };
  std::optional<Drag> _drag;

  UIModule* ui() { return _engine ? _engine->engine().getModules().find<UIModule>() : nullptr; }
  const LayoutBox* layout() {
    UIModule* module = ui();
    return module ? module->layoutOf(_document) : nullptr;
  }
  void sync(Editor& editor, AssetDocument& doc);
  void apply(AssetDocument& doc, const std::string& label, const std::string& html, const std::string& mergeKey = {}) {
    if (html != doc.text()) doc.setText(html, label, mergeKey);
  }
  void select(const UINode* node) { _selected = node ? std::optional(uisource::pathOf(*node)) : std::nullopt; }
  void reveal(const UINode* node) {
    select(node);
    _scrollOutline = true;
  }
  // The text the engine shows: the document with its linked stylesheets
  // inlined from the project (live, and never half-written mid-build), and
  // the selection and its ancestors un-hidden (screens hide parts with a
  // "hidden" class until a script shows them), so what's edited is visible.
  std::string previewText(const Project& project, const std::string& html) const;
  // Never the root: it has no tag to edit.
  const UINode* selected() const {
    const UINode* n = _selected ? uisource::find(_parsed, *_selected) : nullptr;
    return n && n->parent ? n : nullptr;
  }
  // The deepest element whose box holds `point` (game px), as a node of _parsed.
  const UINode* hit(const LayoutBox& box, glm::vec2 point) const;
  const LayoutBox* boxOf(const UINode& node);
  void beginDrag(const UINode* under, glm::vec2 point, ImVec2 mouse);
  void updateDrag(AssetDocument& doc);
  void insert(AssetDocument& doc, const std::string& snippet, const std::string& label);
  void duplicate(AssetDocument& doc, const UINode& node);
  void erase(AssetDocument& doc, const UINode& node);
  void shift(AssetDocument& doc, const UINode& node, int delta);
  void elementMenu(AssetDocument& doc, const UINode& node);
  void drawOutline(AssetDocument& doc, const UINode& node, int depth);
  void drawCanvas(Editor& editor, AssetDocument& doc);
  void drawToolbar(AssetDocument& doc);
};

void UiEditor::sync(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  // An engine of its own on the build, restarted after each build (new images, fonts, stylesheets).
  if (_engineBuild != editor.buildGeneration()) {
    _engineBuild = editor.buildGeneration();
    _engine.reset();
    _document = 0;
    HostedEngine::Options options;
    options.simulate = false;
    options.saveDir = settingsDir() / "preview-saves";
    MuteEngineLog mute;
    _engine = HostedEngine::create(project.buildDir(), options, _engineError);
    _shownRevision = ~0ull;
    if (_engine) {
      const nlohmann::json& config = _engine->engine().getManifest().config;
      const auto renderer = config.value("renderer", nlohmann::json::object());
      const auto window = config.value("window", nlohmann::json::object());
      _gameSize = {renderer.value("logicalWidth", window.value("width", 1280)), renderer.value("logicalHeight", window.value("height", 720))};
    }
  }
  if (doc.revision() == _shownRevision && _selected == _shownSelection) return;
  _shownRevision = doc.revision();
  _shownSelection = _selected;
  _parsed = parseHtml(doc.text());
  _css = _parsed.css;
  for (const UINode* n : uisource::allElements(*_parsed.root)) {
    if (n->tag == "link" && n->attributes.contains("href")) _css += project.readText(n->attributes.at("href"));
  }
  if (_selected && !selected()) _selected.reset();
  if (UIModule* module = ui()) {
    const std::string shown = previewText(project, doc.text());
    if (_document) module->replaceDocument(_document, shown);
    else _document = module->openDocument(shown);
  }
}

std::string UiEditor::previewText(const Project& project, const std::string& html) const {
  std::string out = html;
  // Un-hide the selection's line, deepest first so earlier offsets hold.
  for (const UINode* n = selected(); n && n->parent; n = n->parent) {
    if (!n->hasClass("hidden")) continue;
    std::vector<std::string> shown;
    std::copy_if(n->classes.begin(), n->classes.end(), std::back_inserter(shown), [](const std::string& c) { return c != "hidden"; });
    out = withClasses(out, *n, shown);
  }
  return uisource::inlineStylesheets(out, [&](const std::string& href) { return project.readText(href); });
}

const UINode* UiEditor::hit(const LayoutBox& box, glm::vec2 p) const {
  for (auto it = box.children.rbegin(); it != box.children.rend(); ++it) {  // topmost first
    if (const UINode* found = hit(**it, p)) return found;
  }
  const glm::vec4 r = box.rect;
  if (!box.node || box.node->isText() || !box.node->parent || p.x < r.x || p.y < r.y || p.x >= r.x + r.z || p.y >= r.y + r.w) return nullptr;
  // The engine's tree has the same shape as ours (its text differs only in attributes).
  return uisource::find(_parsed, uisource::pathOf(*box.node));
}

const LayoutBox* UiEditor::boxOf(const UINode& node) {
  const LayoutBox* root = layout();
  return root ? findBox(*root, uisource::pathOf(node)) : nullptr;
}

void UiEditor::beginDrag(const UINode* under, glm::vec2 point, ImVec2 mouse) {
  const UINode* s = selected();
  const LayoutBox* box = s ? boxOf(*s) : nullptr;
  if (!box) return reveal(under);
  const glm::vec4 r = box->rect;
  const bool resize = onHandle(mouse, {_frameOrigin.x + (r.x + r.z) * _frameScale, _frameOrigin.y + (r.y + r.w) * _frameScale});
  const bool inside = point.x >= r.x && point.y >= r.y && point.x < r.x + r.z && point.y < r.y + r.w;
  const bool movable = box->style.position == Position::Absolute;
  if (!resize && !(inside && movable)) return reveal(under);
  // Offsets are against the containing block: the nearest positioned ancestor, else the screen.
  glm::vec4 container{0, 0, static_cast<float>(_gameSize.x), static_cast<float>(_gameSize.y)};
  for (const UINode* a = s->parent; a && a->parent; a = a->parent) {
    const LayoutBox* ab = boxOf(*a);
    if (ab && ab->style.position != Position::Static) {
      container = ab->rect;
      break;
    }
  }
  Drag d{resize ? Drag::Kind::Resize : Drag::Kind::Move, mouse, r, {}};
  // Keep how it's anchored (by the stylesheet too): right/bottom stay right/bottom.
  d.fromRight = box->style.left.unit == Length::Unit::Auto && box->style.right.unit != Length::Unit::Auto;
  d.fromBottom = box->style.top.unit == Length::Unit::Auto && box->style.bottom.unit != Length::Unit::Auto;
  d.startOffset.x = d.fromRight ? pixels(inlineValue(*s, "right")).value_or(container.x + container.z - (r.x + r.z))
                                : pixels(inlineValue(*s, "left")).value_or(r.x - container.x);
  d.startOffset.y = d.fromBottom ? pixels(inlineValue(*s, "bottom")).value_or(container.y + container.w - (r.y + r.w))
                                 : pixels(inlineValue(*s, "top")).value_or(r.y - container.y);
  if (under) d.clicked = uisource::pathOf(*under);
  _drag = d;
}

void UiEditor::updateDrag(AssetDocument& doc) {
  if (!_drag) return;
  const UINode* s = selected();
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !s) {
    if (_drag->kind == Drag::Kind::Move && !_drag->moved) {  // a click: select what's under it
      _selected = _drag->clicked;
      _scrollOutline = true;
    }
    _drag.reset();
    return;
  }
  const ImVec2 m = ImGui::GetMousePos();
  const glm::vec2 delta{std::round((m.x - _drag->startMouse.x) / _frameScale), std::round((m.y - _drag->startMouse.y) / _frameScale)};
  if (!_drag->moved && std::abs(delta.x) < 1 && std::abs(delta.y) < 1) return;
  const bool started = !_drag->moved;
  _drag->moved = true;
  if (_drag->kind == Drag::Kind::Resize) {
    apply(doc, "Resize " + elementLabel(*s),
          uisource::setStyles(doc.text(), *s, {{"width", px(std::max(1.0f, _drag->startRect.z + delta.x))},
                                               {"height", px(std::max(1.0f, _drag->startRect.w + delta.y))}}),
          gestureKey("uiResize", started));
    return;
  }
  const float x = _drag->fromRight ? _drag->startOffset.x - delta.x : _drag->startOffset.x + delta.x;
  const float y = _drag->fromBottom ? _drag->startOffset.y - delta.y : _drag->startOffset.y + delta.y;
  apply(doc, "Move " + elementLabel(*s),
        uisource::setStyles(doc.text(), *s, {{_drag->fromRight ? "right" : "left", px(x)}, {_drag->fromBottom ? "bottom" : "top", px(y)}}),
        gestureKey("uiMove", started));
}

void UiEditor::insert(AssetDocument& doc, const std::string& snippet, const std::string& label) {
  // Into the selected container; right after a selected image, text or inline
  // element (they don't hold boxes); at the top level with nothing selected.
  static const std::set<std::string> kInline = {"span", "b", "i", "strong", "em", "a", "small", "label"};
  const UINode* s = selected();
  const bool after = s && (s->source.openEnd == s->source.end || kInline.contains(s->tag) || !uisource::textOf(*s).value_or("").empty());
  const UINode& target = s ? *s : *_parsed.root;
  Path path = uisource::pathOf(target);
  if (after) ++path.back();
  else path.push_back(static_cast<int>(uisource::elements(target).size()));
  apply(doc, label, uisource::insert(doc.text(), target, snippet, after ? uisource::Place::After : uisource::Place::Inside));
  _selected = path;
}

void UiEditor::duplicate(AssetDocument& doc, const UINode& node) {
  Path copy = uisource::pathOf(node);
  ++copy.back();
  apply(doc, "Duplicate " + elementLabel(node), uisource::duplicate(doc.text(), node));
  _selected = copy;
}

void UiEditor::erase(AssetDocument& doc, const UINode& node) {
  apply(doc, "Delete " + elementLabel(node), uisource::remove(doc.text(), node));
  _selected.reset();
}

void UiEditor::shift(AssetDocument& doc, const UINode& node, int delta) {
  const std::string html = uisource::move(doc.text(), node, delta);
  if (html == doc.text()) return;
  Path path = uisource::pathOf(node);
  path.back() += delta;
  apply(doc, "Move " + elementLabel(node), html);
  _selected = path;
}

void UiEditor::elementMenu(AssetDocument& doc, const UINode& node) {
  if (ImGui::MenuItem(ICON_COPY "  Duplicate", shortcutLabel(ImGuiMod_Ctrl | ImGuiKey_D).c_str())) duplicate(doc, node);
  if (ImGui::MenuItem(ICON_ARROW_UP "  Move Up", nullptr, false, uisource::pathOf(node).back() > 0)) shift(doc, node, -1);
  if (ImGui::MenuItem(ICON_ARROW_DOWN "  Move Down")) shift(doc, node, 1);
  ImGui::Separator();
  if (ImGui::MenuItem(ICON_TRASH "  Delete", "Delete")) erase(doc, node);
}

void UiEditor::drawOutline(AssetDocument& doc, const UINode& node, int depth) {
  for (const UINode* child : uisource::elements(node)) {
    if (child->tag == "link") continue;
    ImGui::PushID(static_cast<int>(child->source.start));
    const bool isSelected = selected() == child;
    const bool hidden = child->hasClass("hidden");
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("##row", {w, 24})) select(child);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) _hovered = uisource::pathOf(*child);
    // Drag rows to reorder, or onto a row's middle to put it inside.
    if (ImGui::BeginDragDropSource()) {
      const std::string path = uisource::pathText(uisource::pathOf(*child));
      ImGui::SetDragDropPayload("JM_UI_ELEMENT", path.data(), path.size());
      ImGui::TextUnformatted(elementLabel(*child).c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      const float y = ImGui::GetMousePos().y - a.y;
      const uisource::Place where = y < 7 ? uisource::Place::Before : y > 17 ? uisource::Place::After : uisource::Place::Inside;
      ImDrawList* d = ImGui::GetWindowDrawList();
      const float lineY = where == uisource::Place::Before ? a.y : a.y + 24;
      if (where == uisource::Place::Inside) d->AddRect(a, {a.x + w, a.y + 24}, theme::u32(theme::accent), theme::radius, 2.0f);
      else d->AddLine({a.x + 8 + depth * 14.0f, lineY}, {a.x + w, lineY}, theme::u32(theme::accent), 2.0f);
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_UI_ELEMENT", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        const Path from = uisource::parsePath({static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize)});
        if (const UINode* moving = uisource::find(_parsed, from)) {
          size_t movedTo = std::string::npos;
          const std::string html = uisource::relocate(doc.text(), *moving, *child, where, &movedTo);
          if (html != doc.text()) {
            apply(doc, "Move " + elementLabel(*moving), html);
            // Select it where it landed.
            const ParsedHtml after = parseHtml(html);
            for (const UINode* n : uisource::allElements(*after.root)) {
              if (n->source.start == movedTo) _selected = uisource::pathOf(*n);
            }
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("menu")) {
      select(child);
      elementMenu(doc, *child);
      ImGui::EndPopup();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (isSelected && _scrollOutline) {
      ImGui::SetScrollHereY(0.4f);
      _scrollOutline = false;
    }
    if (isSelected) draw->AddRectFilled(a, {a.x + w, a.y + 24}, theme::u32(theme::selection), theme::radius);
    else if (hovered) draw->AddRectFilled(a, {a.x + w, a.y + 24}, theme::u32(theme::text, 0.05f), theme::radius);
    float x = a.x + 8 + depth * 14.0f;
    draw->AddText({x, a.y + 4}, theme::u32(isSelected ? theme::accent : theme::textFaint), hidden ? ICON_EYE_SLASH : elementIcon(*child));
    x += 20;
    draw->AddText({x, a.y + 4}, theme::u32(isSelected ? theme::text : theme::textDim, hidden && !isSelected ? 0.6f : 1.0f), child->tag.c_str());
    x += ImGui::CalcTextSize(child->tag.c_str()).x;
    if (!child->id.empty()) {
      const std::string id = "#" + child->id;
      draw->AddText({x, a.y + 4}, theme::u32(theme::accent), id.c_str());
      x += ImGui::CalcTextSize(id.c_str()).x;
    }
    std::string rest;
    for (const std::string& c : child->classes) rest += "." + c;
    if (auto text = uisource::textOf(*child); text && !text->empty()) rest += "  \"" + *text + "\"";
    if (!rest.empty()) {
      draw->PushClipRect(a, {a.x + w - 4, a.y + 24}, true);
      draw->AddText({x, a.y + 4}, theme::u32(theme::textFaint), rest.c_str());
      draw->PopClipRect();
    }
    ImGui::PopID();
    drawOutline(doc, *child, depth + 1);
  }
}

void UiEditor::drawToolbar(AssetDocument& doc) {
  for (const Insert& i : kInserts) {
    if (ui::iconButton(i.label, i.icon, (std::string("Insert ") + i.label + (selected() ? " into the selection" : "")).c_str())) {
      insert(doc, i.snippet, std::string("Insert ") + i.label);
    }
    ImGui::SameLine(0, 2);
  }
}

void UiEditor::drawCanvas(Editor& editor, AssetDocument& doc) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, theme::u32(theme::bg0));
  if (!_engine) {
    ImGui::SetCursorScreenPos(origin);
    ui::emptyState(ICON_HAMMER, "Preview unavailable", _engineError.empty() ? "Build the project to preview its UI." : _engineError.c_str());
    return;
  }
  ImGui::InvisibleButton("##canvas", size);
  const bool hovered = ImGui::IsItemHovered();
  // Zoom: fit, or the wheel / a pinch.
  const float fit = std::min((size.x - 40) / _gameSize.x, (size.y - 40) / _gameSize.y);
  if (const float factor = hovered ? scroll::canvasGesture().zoom : 1.0f; factor != 1.0f) {
    _zoom = std::clamp((_zoom > 0 ? _zoom : fit) * factor, 0.25f, 12.0f);
  }
  const float zoom = _zoom > 0 ? _zoom : fit;
  // Render: the world origin at the canvas center, the game frame around it.
  Renderer2DModule::EditorView view;
  view.zoom = zoom;
  view.showUi = true;
  view.gameSize = _gameSize;
  view.logicalSize = {std::max(1, static_cast<int>(size.x)), std::max(1, static_cast<int>(size.y))};
  _engine->renderer().setEditorView(view);
  const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
  const unsigned texture = _engine->frame(static_cast<int>(size.x * fb), static_cast<int>(size.y * fb), 0.0f);
  draw->AddImage(static_cast<ImTextureID>(texture), origin, {origin.x + size.x, origin.y + size.y}, {0, 1}, {1, 0});
  _frameScale = zoom;
  _frameOrigin = {std::floor(origin.x + size.x * 0.5f - _gameSize.x * zoom * 0.5f), std::floor(origin.y + size.y * 0.5f - _gameSize.y * zoom * 0.5f)};
  const ImVec2 frameEnd{_frameOrigin.x + _gameSize.x * zoom, _frameOrigin.y + _gameSize.y * zoom};
  draw->AddRect({_frameOrigin.x - 1, _frameOrigin.y - 1}, {frameEnd.x + 1, frameEnd.y + 1}, theme::u32(theme::border));
  char sizeLabel[48];
  std::snprintf(sizeLabel, sizeof(sizeLabel), "%d x %d  ·  %.0f%%", _gameSize.x, _gameSize.y, zoom * 100);
  draw->AddText({_frameOrigin.x, _frameOrigin.y - 18}, theme::u32(theme::textFaint), sizeLabel);

  // Hover and selection outlines, from the engine's layout.
  const LayoutBox* root = layout();
  auto toScreen = [&](glm::vec4 r) {
    return std::pair<ImVec2, ImVec2>{{_frameOrigin.x + r.x * zoom, _frameOrigin.y + r.y * zoom},
                                     {_frameOrigin.x + (r.x + r.z) * zoom, _frameOrigin.y + (r.y + r.w) * zoom}};
  };
  const ImVec2 m = ImGui::GetMousePos();
  const glm::vec2 point{(m.x - _frameOrigin.x) / zoom, (m.y - _frameOrigin.y) / zoom};
  const UINode* under = hovered && root ? hit(*root, point) : nullptr;
  if (under) _hovered = uisource::pathOf(*under);
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) beginDrag(under, point, m);
  updateDrag(doc);
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && under && uisource::textOf(*under)) ImGui::OpenPopup("inlineText");
  if (const UINode* h = _hovered ? uisource::find(_parsed, *_hovered) : nullptr; h && h != selected()) {
    if (const LayoutBox* box = boxOf(*h)) {
      auto [a, b] = toScreen(box->rect);
      draw->AddRect(a, b, theme::u32(theme::info, 0.8f), 0, 1.0f);
    }
  }
  if (const UINode* s = selected()) {
    if (const LayoutBox* box = boxOf(*s)) {
      const glm::vec4 r = box->rect;
      auto [a, b] = toScreen(r);
      draw->AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, theme::u32(theme::accent), 0, 2.0f);
      // The resize handle, and cursors saying what a drag does.
      draw->AddRectFilled({b.x - 4, b.y - 4}, {b.x + 4, b.y + 4}, theme::u32(theme::accent));
      draw->AddRect({b.x - 4, b.y - 4}, {b.x + 4, b.y + 4}, theme::u32(theme::bg0));
      const bool movable = box->style.position == Position::Absolute;
      if (hovered && (onHandle(m, b) || (_drag && _drag->kind == Drag::Kind::Resize))) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
      else if (hovered && movable && m.x >= a.x && m.y >= a.y && m.x < b.x && m.y < b.y) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      char dims[32];
      std::snprintf(dims, sizeof(dims), "  %.0f x %.0f", r.z, r.w);
      const std::string label = elementLabel(*s) + dims;
      ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
      const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
      const ImVec2 p{a.x - 1, a.y - ts.y - 6 < _frameOrigin.y - 40 ? b.y + 3 : a.y - ts.y - 6};
      draw->AddRectFilled(p, {p.x + ts.x + 10, p.y + ts.y + 4}, theme::u32(theme::accent), theme::radius);
      draw->AddText({p.x + 5, p.y + 2}, theme::u32(theme::bg0), label.c_str());
      ImGui::PopFont();
    }
  }
  // Double-click a text element to retype it right there.
  if (ImGui::BeginPopup("inlineText")) {
    const UINode* s = selected();
    if (auto text = s ? uisource::textOf(*s) : std::nullopt) {
      if (ImGui::IsWindowAppearing()) _draft = *text, ImGui::SetKeyboardFocusHere();
      ImGui::SetNextItemWidth(260);
      if (ImGui::InputText("##text", &_draft)) apply(doc, "Edit Text", uisource::setText(doc.text(), *s, _draft), "inlineText");
      if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    } else {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  // Images dropped on the canvas become <img> elements.
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
      const std::string path(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
      const AssetKind kind = assetKindOf(path.substr(0, path.find('#')));
      if (kind == AssetKind::Image || kind == AssetKind::Atlas) {
        int w = 32, h = 32;
        if (auto pic = Thumbnails::instance().get(*editor.project(), path)) w = static_cast<int>(pic->size.x), h = static_cast<int>(pic->size.y);
        insert(doc, "<img src=\"" + path + "\" style=\"width: " + std::to_string(w) + "px; height: " + std::to_string(h) + "px\">", "Insert Image");
      }
    }
    ImGui::EndDragDropTarget();
  }
  // Escape climbs to the parent (Delete and Duplicate come through the Edit menu).
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    if (const UINode* s = selected()) select(s->parent && s->parent->parent ? s->parent : nullptr);
  }
}

void UiEditor::draw(Editor& editor, AssetDocument& doc) {
  sync(editor, doc);
  _hovered.reset();

  const float right = ui::beginDocumentBar(ICON_BROWSER, doc.title().c_str(), doc.path().c_str());
  const float toolsW = std::size(kInserts) * (ImGui::GetFrameHeight() + 2) + 16, zoomW = 64, codeW = 90;
  ImGui::SameLine(right - toolsW - zoomW - codeW - 16);
  drawToolbar(doc);
  ImGui::SameLine(0, 12);
  if (ui::button(_zoom > 0 ? "1:1" : "Fit", {zoomW, 0})) _zoom = _zoom > 0 ? 0 : 1;
  ui::tooltip(_zoom > 0 ? "Fit the frame (scroll to zoom)" : "Actual pixels (scroll to zoom)");
  ImGui::SameLine(0, 4);
  ImGui::PushStyleColor(ImGuiCol_Button, _code ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
  if (ImGui::Button(ICON_CODE "  Code", {codeW, 0})) _code = !_code;
  ImGui::PopStyleColor();
  ui::tooltip("Edit the HTML directly");
  ui::endDocumentBar();

  // Outline | canvas (or the code).
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6, 8});
  ImGui::BeginChild("##outline", {230, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ui::sectionLabel("Elements");
  drawOutline(doc, *_parsed.root, 0);
  if (uisource::elements(*_parsed.root).empty()) {
    ui::smallText("Empty. Insert a box, row or text above.", theme::textFaint);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 0);
  ImGui::BeginChild("##view", {0, 0}, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  if (_code) {
    std::string text = doc.text();
    ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::bg0);
    if (ImGui::InputTextMultiline("##code", &text, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_AllowTabInput)) {
      doc.setText(text, "Edit HTML", "code");
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
  } else {
    drawCanvas(editor, doc);
  }
  ImGui::EndChild();
}

bool UiEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  const UINode* node = selected();
  if (!node) {
    ui::emptyState(ICON_BROWSER, "Pick an element", "Click an element on the canvas or in the outline to edit its text, id, classes and style.");
    return true;
  }
  const std::string html = doc.text();
  const Project& project = *editor.project();

  // Header: the element and its ancestors, each clickable.
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, "%s", elementIcon(*node));
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(elementLabel(*node).c_str());
  ImGui::PopFont();
  std::vector<const UINode*> ancestors;
  for (const UINode* a = node->parent; a && a->parent; a = a->parent) ancestors.insert(ancestors.begin(), a);
  ImGui::PushFont(nullptr, theme::sizeSmall);
  for (const UINode* a : ancestors) {
    ImGui::PushID(a);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
    if (ImGui::SmallButton(elementLabel(*a).c_str())) select(a);
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::SameLine(0, 2);
    ImGui::TextColored(theme::textFaint, ICON_CARET_RIGHT);
    ImGui::SameLine(0, 2);
  }
  ImGui::NewLine();
  ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::Dummy({0, 4});

  if (!ui::beginProperties("element", 104)) return true;
  // Content.
  if (auto text = uisource::textOf(*node)) {
    ui::propertyRow("Text");
    std::string value = *text;
    if (ImGui::InputTextMultiline("##text", &value, {-1, 54})) apply(doc, "Edit Text", uisource::setText(html, *node, value), "text");
  }
  // Identity.
  ui::propertyRow("Id", "Scripts change elements by id: UI.setText(\"score\", ...)");
  std::string id = node->id;
  if (ImGui::InputTextWithHint("##id", "none", &id, ImGuiInputTextFlags_CharsNoBlank)) {
    apply(doc, "Set Id", uisource::setAttribute(html, *node, "id", id.empty() ? std::nullopt : std::optional(id)), "id");
  }
  if (!node->id.empty()) {  // how to name it to an agent: "assets/ui/hud.ui.html#score"
    ui::propertyRow("Reference", "Copies the element's reference, to paste to an agent");
    const std::string reference = doc.path() + "#" + node->id;
    if (ui::button((std::string(ICON_COPY "  ") + reference).c_str(), {-1, 0})) ImGui::SetClipboardText(reference.c_str());
  }
  ui::propertyRow("Classes", "Style rules from the stylesheets apply by class");
  for (size_t i = 0; i < node->classes.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    bool removed = false;
    ui::chip("class", node->classes[i].c_str(), true, &removed);
    if (removed) {
      std::vector<std::string> kept = node->classes;
      kept.erase(kept.begin() + static_cast<long>(i));
      apply(doc, "Remove Class", withClasses(html, *node, kept));
    }
    ImGui::PopID();
    ImGui::SameLine(0, 4);
  }
  if (ui::iconButton("addClass", ICON_PLUS, "Add a class", false, 0, ImGui::GetFrameHeight() - 2)) ImGui::OpenPopup("classes");
  ImGui::NewLine();
  if (ImGui::BeginPopup("classes")) {
    if (ImGui::IsWindowAppearing()) _draft.clear(), ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(200);
    const bool entered = ImGui::InputTextWithHint("##new", "class name", &_draft, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
    std::string pick = entered ? _draft : "";
    for (const std::string& c : uisource::classesIn(_css)) {
      if (node->hasClass(c) || (!_draft.empty() && ui::fuzzyScore(c, _draft) < 0)) continue;
      if (ImGui::Selectable(("." + c).c_str())) pick = c;
    }
    if (!pick.empty()) {
      std::vector<std::string> classes = node->classes;
      if (!node->hasClass(pick)) classes.push_back(pick);
      apply(doc, "Add Class " + pick, withClasses(html, *node, classes));
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (node->tag == "img") {
    ui::propertyRow("Image", "An image or atlas region; drag one here from Assets");
    std::string src = node->attributes.contains("src") ? node->attributes.at("src") : "";
    if (ImGui::InputTextWithHint("##src", "assets/...", &src)) apply(doc, "Set Image", uisource::setAttribute(html, *node, "src", src), "src");
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
        const std::string path(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
        apply(doc, "Set Image", uisource::setAttribute(html, *node, "src", path));
      }
      ImGui::EndDragDropTarget();
    }
    if (auto pic = src.empty() ? std::nullopt : Thumbnails::instance().get(project, src)) {
      const ImVec2 a = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      ImGui::Dummy({w, 64});
      widgets::checker(ImGui::GetWindowDrawList(), a, {a.x + w, a.y + 64});
      widgets::fitted(ImGui::GetWindowDrawList(), *pic, {a.x + 4, a.y + 4}, {a.x + w - 4, a.y + 60});
    }
  }
  ui::endProperties();

  // Style, inline on the element. Set values carry the accent mark; right-click one to clear it.
  const LayoutBox* laidOut = boxOf(*node);
  for (const StyleSection& section : styleSections()) {
    if (!ui::componentHeader(section.title, section.icon, section.title, nullptr)) continue;
    if (!ui::beginProperties(section.title, 104)) continue;
    for (const StyleProperty& prop : section.properties) {
      ImGui::PushID(prop.name);
      std::string value = inlineValue(*node, prop.name);
      // Empty clears it; `merge` folds a run of typing into one undo step.
      auto set = [&](const std::string& to, const std::string& label, const std::string& merge = {}) {
        apply(doc, label + " " + prop.name, uisource::setStyles(html, *node, {{prop.name, to}}), merge);
      };
      ui::propertyRow(prop.label, prop.name, !value.empty());
      if (ImGui::BeginPopupContextItem("clear")) {
        if (ImGui::MenuItem(ICON_X "  Clear", nullptr, false, !value.empty())) set("", "Clear");
        ImGui::EndPopup();
      }
      const std::string computed = laidOut ? computedValue(*laidOut, prop.name) : "";
      const char* hint = computed.empty() ? prop.hint : computed.c_str();
      switch (prop.kind) {
        case StyleProperty::Choice: {
          if (value.empty()) ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
          const bool open = ui::beginCombo("##v", value.empty() ? hint : value.c_str());
          if (value.empty()) ImGui::PopStyleColor();
          if (open) {
            if (ImGui::Selectable("Not set", value.empty())) set("", "Set");
            for (const char* c : prop.choices) {
              if (ImGui::Selectable(c, value == c)) set(c, "Set");
            }
            ImGui::EndCombo();
          }
          break;
        }
        case StyleProperty::Color: {
          // The swatch shows the effective color.
          const auto rgba = parseHex(value).or_else([&] { return parseHex(computed); }).value_or(std::array<float, 4>{0, 0, 0, 1});
          float c[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
          if (ImGui::InputTextWithHint("##hex", hint, &value)) set(value, "Set", prop.name);
          ImGui::SameLine(0, 4);
          if (ImGui::ColorEdit4("##swatch", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            set(toHex(c), "Set", prop.name);
          }
          break;
        }
        case StyleProperty::Text: {
          if (ImGui::InputTextWithHint("##v", hint, &value)) set(value, "Set", prop.name);
          break;
        }
      }
      ImGui::PopID();
    }
    ui::endProperties();
  }
  ImGui::Dummy({0, 8});
  const float w = ImGui::GetContentRegionAvail().x;
  if (ui::button(ICON_COPY "  Duplicate", {(w - 4) * 0.5f, 0})) duplicate(doc, *node);
  ImGui::SameLine(0, 4);
  if (ui::button(ICON_TRASH "  Delete", {(w - 4) * 0.5f, 0})) erase(doc, *node);
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeUiEditor() { return std::make_unique<UiEditor>(); }
