// The Scene view: the edit preview with a free camera, picking, gizmos,
// drag-to-edit, box selection, tile painting and asset drops.

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Scroll.hpp"
#include "editors/UiSource.hpp"
#include "ui/Layout.hpp"
#include "ui/UIModule.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"
#include "Ui.hpp"
#include "renderer2d/Renderer2D.hpp"
#include "TiledFiles.hpp"
#include "editors/EditorWidgets.hpp"
#include "tilemap/TileGrid.hpp"

namespace {

constexpr float kMinZoom = 0.05f, kMaxZoom = 40.0f;
constexpr float kArrow = 72.0f;      // gizmo axis length, points
constexpr float kArrowStart = 18.0f; // presses nearer the center move freely
constexpr float kRing = 64.0f;       // rotate ring radius
constexpr float kHandle = 5.0f;      // scale handle half size
constexpr float kHitSlop = 7.0f;

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const ImVec2 ab{b.x - a.x, b.y - a.y}, ap{p.x - a.x, p.y - a.y};
  const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / std::max(1e-4f, ab.x * ab.x + ab.y * ab.y), 0.0f, 1.0f);
  const float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
  return std::sqrt(dx * dx + dy * dy);
}

void arrow(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float thickness) {
  draw->AddLine(from, to, color, thickness);
  const float dx = to.x - from.x, dy = to.y - from.y;
  const float len = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
  const ImVec2 d{dx / len, dy / len}, n{-d.y, d.x};
  draw->AddTriangleFilled({to.x + d.x * 10, to.y + d.y * 10}, {to.x + n.x * 5, to.y + n.y * 5},
                          {to.x - n.x * 5, to.y - n.y * 5}, color);
}

glm::vec2 xyOf(const Json& v, glm::vec2 fallback) {
  return v.is_array() && v.size() >= 2 ? glm::vec2(v[0].get<float>(), v[1].get<float>()) : fallback;
}

// A position array moved in x and y, keeping its z.
Json movedBy(const Json& position, glm::vec2 d) {
  const glm::vec2 p = xyOf(position, glm::vec2(0.0f));
  return Json::array({p.x + d.x, p.y + d.y, position.is_array() && position.size() > 2 ? position[2] : Json(0)});
}

// Which part of the tool's gizmo (centered at p, over an entity with these screen corners) is under the mouse.
struct GizmoHit {
  bool free = false, x = false, y = false, ring = false;
  unsigned corners = 0;  // bit i: scale handle i
};

GizmoHit gizmoHit(Tool tool, ImVec2 p, const std::array<ImVec2, 4>& corners, ImVec2 mouse) {
  GizmoHit hit;
  if (tool == Tool::Move) {
    hit.free = std::abs(mouse.x - p.x - 9) < 9 && std::abs(mouse.y - p.y + 9) < 9;
    hit.x = !hit.free && distanceToSegment(mouse, {p.x + kArrowStart, p.y}, {p.x + kArrow, p.y}) < kHitSlop;
    hit.y = !hit.free && !hit.x && distanceToSegment(mouse, {p.x, p.y - kArrowStart}, {p.x, p.y - kArrow}) < kHitSlop;
  } else if (tool == Tool::Rotate) {
    hit.ring = std::abs(std::hypot(mouse.x - p.x, mouse.y - p.y) - kRing) < kHitSlop;
  } else if (tool == Tool::Scale) {
    for (unsigned i = 0; i < 4; ++i) {
      if (std::abs(mouse.x - corners[i].x) < kHandle + 3 && std::abs(mouse.y - corners[i].y) < kHandle + 3) hit.corners |= 1u << i;
    }
  }
  return hit;
}

// Tiled flips (Editor::TileBrush::flips) as which corner of an image each
// screen corner shows: uvs in the order top-left, top-right, bottom-right, bottom-left.
std::array<ImVec2, 4> flippedUvs(const Thumbnails::Picture& p, uint32_t flips) {
  std::array<ImVec2, 4> uv = {p.uv0, ImVec2{p.uv1.x, p.uv0.y}, p.uv1, ImVec2{p.uv0.x, p.uv1.y}};
  if (flips & tiled::kFlipD) std::swap(uv[1], uv[3]);
  if (flips & tiled::kFlipH) std::swap(uv[0], uv[1]), std::swap(uv[3], uv[2]);
  if (flips & tiled::kFlipV) std::swap(uv[0], uv[3]), std::swap(uv[1], uv[2]);
  return uv;
}

}  // namespace

glm::vec2 ScenePanel::toWorld(ImVec2 s) const {
  return {_center.x + (s.x - _origin.x - _size.x * 0.5f) / _zoom, _center.y - (s.y - _origin.y - _size.y * 0.5f) / _zoom};
}

ImVec2 ScenePanel::toScreen(glm::vec2 w) const {
  return {_origin.x + _size.x * 0.5f + (w.x - _center.x) * _zoom, _origin.y + _size.y * 0.5f - (w.y - _center.y) * _zoom};
}

glm::vec2 ScenePanel::snapped(glm::vec2 p, bool force) const {
  const bool on = _snap != ImGui::GetIO().KeyCtrl || force;  // Ctrl flips snapping for one drag
  if (!on || _gridSize <= 0) return p;
  return glm::round(p / _gridSize) * _gridSize;
}

void ScenePanel::setZoom(float zoom) { _targetZoom = std::clamp(zoom, kMinZoom, kMaxZoom); }

void ScenePanel::frameSelection(Editor& editor) {
  std::optional<std::pair<glm::vec2, glm::vec2>> box;
  for (EntityUid uid : editor.selection()) {
    if (auto b = editor.preview().bounds(uid)) {
      if (!box) box = std::pair{b->min, b->max};
      box->first = glm::min(box->first, b->min);
      box->second = glm::max(box->second, b->max);
    }
  }
  if (!box) {
    frameAll(editor);
    return;
  }
  const glm::vec2 size = glm::max(box->second - box->first, glm::vec2(32.0f));
  _targetCenter = (box->first + box->second) * 0.5f;
  _targetZoom = std::clamp(std::min(_size.x / size.x, _size.y / size.y) * 0.6f, kMinZoom, 8.0f);
}

void ScenePanel::frameAll(Editor& editor) {
  // The game's view, and everything in the scene.
  const glm::vec2 game = glm::vec2(editor.preview().gameSize());
  glm::vec2 lo = -game * 0.5f, hi = game * 0.5f;
  if (auto extent = editor.preview().extent()) {
    lo = glm::min(lo, extent->first);
    hi = glm::max(hi, extent->second);
  }
  const glm::vec2 size = glm::max(hi - lo, glm::vec2(64.0f));
  _targetCenter = (lo + hi) * 0.5f;
  _targetZoom = std::clamp(std::min(_size.x / size.x, _size.y / size.y) * 0.9f, kMinZoom, 8.0f);
}

void ScenePanel::draw(Editor& editor, float dt) {
  SceneDocument* scene = editor.scene();
  if (!scene) {
    if (ui::emptyState(ICON_FILM_SLATE, "No scene open", "Pick one from the scene menu in the toolbar, or start a new one.",
                       "New Scene")) {
      editor.commands().run("scene.new");
    }
    return;
  }
  Preview& preview = editor.preview();
  _origin = ImGui::GetCursorScreenPos();
  _size = ImGui::GetContentRegionAvail();
  if (_size.x < 4 || _size.y < 4) return;

  if (!preview.running()) {
    if (editor.cli().busy()) {
      const ImVec2 c{_origin.x + _size.x * 0.5f, _origin.y + _size.y * 0.45f};
      ImGui::SetCursorScreenPos({c.x - 12, c.y - 30});
      ui::spinner(12, theme::u32(theme::accent));
      ImGui::SetCursorScreenPos({c.x - ImGui::CalcTextSize("Building the project").x * 0.5f, c.y + 6});
      ui::dimText("Building the project");
      const std::string last = editor.cli().lastLine();
      ImGui::PushFont(theme::fonts().mono, theme::sizeSmall);
      ImGui::SetCursorScreenPos({c.x - std::min(ImGui::CalcTextSize(last.c_str()).x, _size.x - 40) * 0.5f, c.y + 30});
      ImGui::PushTextWrapPos(_origin.x + _size.x - 20);
      ImGui::TextColored(theme::textFaint, "%s", last.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopFont();
    } else if (ui::emptyState(ICON_WARNING_CIRCLE, "The scene can't be shown",
                              preview.error().empty() ? "Build the project to see it here." : preview.error().c_str(),
                              "Build")) {
      editor.build();
    }
    return;
  }

  // Frame a scene the first time it's shown (once the dock has settled the
  // view's size), then ease toward any target view.
  const bool sizeSettled = _size.x == _lastSize.x && _size.y == _lastSize.y;
  _lastSize = _size;
  if (_framedScene != scene->path() && sizeSettled) {
    _framedScene = scene->path();
    // Snap to the scene's tiles when it has a tile map.
    for (size_t i = 0; i < scene->size(); ++i) {
      if (const TileGrid* grid = preview.tileGrid(scene->uid(i))) {
        _gridSize = grid->tileSize().x;
        break;
      }
    }
    // Where the view was last time, else everything in frame.
    if (auto camera = sceneCamera(*editor.project(), scene->path())) {
      _center = {(*camera)[0], (*camera)[1]};
      _zoom = std::clamp((*camera)[2], kMinZoom, kMaxZoom);
      _targetCenter.reset();
      _targetZoom.reset();
    } else {
      frameAll(editor);
      _center = *_targetCenter;
      _zoom = *_targetZoom;
    }
    _savedCamera = {_center.x, _center.y, _zoom};
  }
  // Remember the view once it has rested a moment.
  const std::array<float, 3> camera{_center.x, _center.y, _zoom};
  if (camera != _savedCamera && _drag == Drag::None && !_targetCenter && !_targetZoom) {
    if ((_cameraRestSince += dt) > 1.0f) {
      rememberSceneCamera(*editor.project(), scene->path(), camera);
      _savedCamera = camera;
    }
  } else {
    _cameraRestSince = 0;
  }
  const float ease = 1.0f - std::exp(-dt * 16.0f);
  if (_targetCenter) {
    _center += (*_targetCenter - _center) * ease;
    if (glm::length(*_targetCenter - _center) * _zoom < 0.5f) _center = *_targetCenter, _targetCenter.reset();
  }
  if (_targetZoom) {
    _zoom += (*_targetZoom - _zoom) * ease;
    if (std::abs(*_targetZoom - _zoom) < 0.001f * *_targetZoom) _zoom = *_targetZoom, _targetZoom.reset();
  }

  const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
  const unsigned texture = preview.render(_center, _zoom, static_cast<int>(_size.x * scale), static_cast<int>(_size.y * scale),
                                          scale, _showUi, _showColliders, dt);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddImage(static_cast<ImTextureID>(texture), _origin, {_origin.x + _size.x, _origin.y + _size.y}, {0, 1}, {1, 0});

  // One button covers the view and takes every mouse button.
  ImGui::SetCursorScreenPos(_origin);
  // The toolbar and tile palette float over it and are drawn after: let them take the mouse.
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##view", _size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                              ImGuiButtonFlags_MouseButtonMiddle);
  ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
  _hovered = ImGui::IsItemHovered();
  if (_hovered) _cursorWorld = toWorld(ImGui::GetMousePos());
  drawDropTarget(editor);

  draw->PushClipRect(_origin, {_origin.x + _size.x, _origin.y + _size.y}, true);
  if (_showGrid) drawGrid(draw);
  if (_showGameFrame) drawGameFrame(editor, draw);
  handleInput(editor);
  drawSelection(editor, draw);
  if (isTileTool(editor.tool())) {
    handleTilePainting(editor);
  } else {
    drawGizmo(editor, draw);
    drawShapeHandles(editor, draw);
  }
  if (_drag == Drag::Box) {
    const ImVec2 a = toScreen(_dragStart), b = ImGui::GetMousePos();
    draw->AddRectFilled(ImMin(a, b), ImMax(a, b), theme::u32(theme::accent, 0.08f));
    draw->AddRect(ImMin(a, b), ImMax(a, b), theme::u32(theme::accent, 0.7f));
  }
  draw->PopClipRect();
  // Every drag ends with its buttons, including one whose finishing code a mid-drag tool switch skipped.
  if (!ImGui::IsAnyMouseDown()) _drag = Drag::None;

  drawOverlayToolbar(editor);
  if (isTileTool(editor.tool())) drawTilePalette(editor);
  if (scene->isPrefab()) {
    // Editing a prefab changes every instance of it: say so where the work
    // happens, with the way back to the scene it was opened from.
    const std::string text = std::string(ICON_CUBE "  ") + scene->title() + "  \xC2\xB7  editing the prefab changes every instance";
    const std::string back = editor.returnScene();
    ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    ImGui::PopFont();
    const std::string backLabel = std::string(ICON_ARROW_LEFT "  Back to ") + std::filesystem::path(back).stem().stem().string();
    const float backWidth = back.empty() ? 0.0f : ImGui::CalcTextSize(backLabel.c_str()).x + 24;
    const ImVec2 p{_origin.x + 10, _origin.y + 10};
    const float h = 30.0f;
    const ImVec2 q{p.x + ts.x + 24 + (back.empty() ? 0 : backWidth + 8), p.y + h};
    draw->AddRectFilled(p, q, theme::u32(theme::bg1, 0.95f), theme::radiusOverlay);
    draw->AddRectFilled(p, q, theme::u32(theme::info, 0.14f), theme::radiusOverlay);
    draw->AddRect(p, q, theme::u32(theme::info, 0.55f), theme::radiusOverlay);
    ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
    draw->AddText({p.x + 12, p.y + (h - ts.y) * 0.5f}, theme::u32(theme::info), text.c_str());
    ImGui::PopFont();
    if (!back.empty()) {
      ImGui::SetCursorScreenPos({p.x + ts.x + 24, p.y + 3});
      if (ui::button(backLabel.c_str(), {backWidth, h - 6})) editor.returnFromPrefab();
      ui::tooltip("Return to the scene (asks to save the prefab first)");
    }
  }
}

void ScenePanel::drawGrid(ImDrawList* draw) {
  // Grid lines every _gridSize world units, doubling the spacing until lines
  // are at least 8 points apart; every fourth line stronger, axes tinted.
  float step = _gridSize;
  while (step * _zoom < 8.0f) step *= 2.0f;
  const glm::vec2 lo = toWorld({_origin.x, _origin.y + _size.y}), hi = toWorld({_origin.x + _size.x, _origin.y});
  const float fade = std::clamp((step * _zoom - 8.0f) / 16.0f, 0.0f, 1.0f);
  const ImU32 minor = theme::u32(theme::text, 0.035f * fade + 0.02f), major = theme::u32(theme::text, 0.07f);
  for (float x = std::floor(lo.x / step) * step; x <= hi.x; x += step) {
    const float sx = std::round(toScreen({x, 0}).x) + 0.5f;
    const bool strong = std::fmod(std::abs(x), step * 4) < 0.5f;
    draw->AddLine({sx, _origin.y}, {sx, _origin.y + _size.y}, std::abs(x) < 0.5f ? theme::u32(theme::axisY, 0.35f) : strong ? major : minor);
  }
  for (float y = std::floor(lo.y / step) * step; y <= hi.y; y += step) {
    const float sy = std::round(toScreen({0, y}).y) + 0.5f;
    const bool strong = std::fmod(std::abs(y), step * 4) < 0.5f;
    draw->AddLine({_origin.x, sy}, {_origin.x + _size.x, sy}, std::abs(y) < 0.5f ? theme::u32(theme::axisX, 0.35f) : strong ? major : minor);
  }
}

void ScenePanel::drawGameFrame(Editor& editor, ImDrawList* draw) {
  // What the game camera sees when the scene starts (centered on the origin).
  const glm::vec2 half = glm::vec2(editor.preview().gameSize()) * 0.5f;
  if (half.x <= 0) return;
  const ImVec2 a = toScreen({-half.x, half.y}), b = toScreen({half.x, -half.y});
  // Dim the world outside the frame a little, so the play area reads.
  const ImU32 dim = theme::u32(theme::bg0, 0.18f);
  draw->AddRectFilled(_origin, {_origin.x + _size.x, a.y}, dim);
  draw->AddRectFilled({_origin.x, b.y}, {_origin.x + _size.x, _origin.y + _size.y}, dim);
  draw->AddRectFilled({_origin.x, a.y}, {a.x, b.y}, dim);
  draw->AddRectFilled({b.x, a.y}, {_origin.x + _size.x, b.y}, dim);
  draw->AddRect(a, b, theme::u32(theme::text, 0.35f), 0.0f, 1.0f);
  char label[48];
  std::snprintf(label, sizeof(label), ICON_MONITOR "  %d x %d", editor.preview().gameSize().x, editor.preview().gameSize().y);
  ImGui::PushFont(nullptr, theme::sizeSmall);
  draw->AddText({a.x + 1, a.y - ImGui::GetTextLineHeight() - 3}, theme::u32(theme::textDim, 0.8f), label);
  ImGui::PopFont();
}

void ScenePanel::drawSelection(Editor& editor, ImDrawList* draw) {
  Preview& preview = editor.preview();
  SceneDocument& scene = *editor.scene();
  auto outline = [&](EntityUid uid, ImU32 color, float thickness) {
    auto b = preview.bounds(uid);
    if (!b) return;
    if (b->point) {
      const ImVec2 c = toScreen(b->position);
      draw->AddCircle(c, 7.0f, color, 0, thickness);
      draw->AddCircleFilled(c, 2.5f, color);
      return;
    }
    ImVec2 pts[4];
    for (int i = 0; i < 4; ++i) pts[i] = toScreen(b->corners[i]);
    draw->AddPolyline(pts, 4, color, thickness, ImDrawFlags_Closed);
  };

  // Entities with no visual get a quiet marker so they can be found and clicked.
  for (size_t i = 0; i < scene.size(); ++i) {
    const EntityUid uid = scene.uid(i);
    if (auto b = preview.bounds(uid); b && b->point && !editor.isSelected(uid)) {
      draw->AddCircle(toScreen(b->position), 5.0f, theme::u32(theme::text, 0.35f), 0, 1.0f);
    }
  }
  // A small chip beside the cursor (hover names, drag readouts).
  auto chip = [&](const std::string& text, ImVec4 color) {
    ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const ImVec2 m = ImGui::GetMousePos();
    const ImVec2 p{m.x + 14, m.y + 14};
    draw->AddRectFilled(p, {p.x + ts.x + 12, p.y + ts.y + 6}, theme::u32(theme::bg0, 0.88f), theme::radius);
    draw->AddText({p.x + 6, p.y + 3}, theme::u32(color), text.c_str());
    ImGui::PopFont();
  };
  if (_hovered && _drag == Drag::None && !isTileTool(editor.tool())) {
    const auto hits = preview.pick(_cursorWorld);
    if (!hits.empty() && !editor.isSelected(hits.front())) {
      outline(hits.front(), theme::u32(theme::text, 0.45f), 1.0f);
      if (const int i = scene.indexOf(hits.front()); i >= 0) chip(scene.displayName(static_cast<size_t>(i)), theme::textDim);
    }
  }
  for (EntityUid uid : editor.selection()) outline(uid, theme::u32(theme::accent), uid == editor.primary() ? 2.0f : 1.5f);
  // While dragging, the value being set.
  const auto b = preview.bounds(editor.primary());
  const Json* primary = b && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) ? scene.find(editor.primary()) : nullptr;
  if (!primary) return;
  const auto transform = [&](const char* field) { return fieldValue(*editor.project(), *primary, "TransformComponent", field); };
  char text[64] = "";
  if (_drag == Drag::Move || _drag == Drag::MoveX || _drag == Drag::MoveY) {
    std::snprintf(text, sizeof(text), "%.0f, %.0f", b->position.x, b->position.y);
  } else if (_drag == Drag::Rotate) {
    const Json r = transform("rotation");
    std::snprintf(text, sizeof(text), "%.1f\xC2\xB0", (r.is_number() ? r.get<float>() : 0.0f) * 180.0f / glm::pi<float>());
  } else if (_drag == Drag::Scale) {
    const Json s = transform("scale");
    if (s.is_array() && s.size() >= 2) std::snprintf(text, sizeof(text), "%.0f x %.0f", s[0].get<float>(), s[1].get<float>());
  }
  if (*text) chip(text, theme::accentBright);
}

void ScenePanel::drawGizmo(Editor& editor, ImDrawList* draw) {
  auto b = editor.selection().empty() ? std::nullopt : editor.preview().bounds(editor.primary());
  if (!b) return;
  const ImVec2 p = toScreen(b->position), mouse = ImGui::GetMousePos();
  std::array<ImVec2, 4> corners;
  for (int i = 0; i < 4; ++i) corners[i] = toScreen(b->corners[i]);
  const GizmoHit hit = _drag == Drag::None && _hovered ? gizmoHit(editor.tool(), p, corners, mouse) : GizmoHit{};
  auto color = [&](Drag d, bool hot, ImVec4 idle, float alpha = 1.0f) { return theme::u32(hot || _drag == d ? theme::warning : idle, alpha); };

  switch (editor.tool()) {
    case Tool::Move:
      arrow(draw, p, {p.x + kArrow, p.y}, color(Drag::MoveX, hit.x, theme::axisX), 2.5f);
      arrow(draw, p, {p.x, p.y - kArrow}, color(Drag::MoveY, hit.y, theme::axisY), 2.5f);
      draw->AddRectFilled({p.x + 2, p.y - 16}, {p.x + 16, p.y - 2}, color(Drag::Move, hit.free, theme::axisZ, 0.55f), 2.0f);
      draw->AddCircleFilled(p, 3.5f, theme::u32(theme::text));
      break;
    case Tool::Rotate:
      draw->AddCircle(p, kRing, color(Drag::Rotate, hit.ring, theme::axisZ), 64, 2.5f);
      if (_drag == Drag::Rotate) draw->AddLine(p, mouse, theme::u32(theme::warning, 0.6f), 1.0f);
      draw->AddCircleFilled(p, 3.5f, theme::u32(theme::text));
      break;
    case Tool::Scale:
      for (unsigned i = 0; i < 4; ++i) {
        const ImVec2 c = corners[i], lo{c.x - kHandle, c.y - kHandle}, hi{c.x + kHandle, c.y + kHandle};
        draw->AddRectFilled(lo, hi, color(Drag::Scale, (hit.corners & (1u << i)) != 0, theme::text));
        draw->AddRect(lo, hi, theme::u32(theme::accent), 0.0f, 1.5f);
      }
      break;
    default:
      break;
  }
}

void ScenePanel::handleInput(Editor& editor) {
  ImGuiIO& io = ImGui::GetIO();
  SceneDocument& scene = *editor.scene();
  Preview& preview = editor.preview();
  const ImVec2 mouse = ImGui::GetMousePos();
  const glm::vec2 world = toWorld(mouse);
  const Project& project = *editor.project();

  // Trackpad scrolling pans, as in design tools; a mouse wheel or a pinch zooms toward the cursor.
  if (_hovered) {
    const scroll::Gesture g = scroll::canvasGesture();
    if (g.pan.x != 0.0f || g.pan.y != 0.0f) {
      _center += glm::vec2(g.pan.x, g.pan.y) / _zoom;
      _targetCenter.reset();
    }
    if (g.zoom != 1.0f) {
      const float zoom = std::clamp((_targetZoom ? *_targetZoom : _zoom) * g.zoom, kMinZoom, kMaxZoom);
      _center = world - (world - _center) * (_zoom / zoom);
      _zoom = zoom;
      _targetZoom.reset();
      _targetCenter.reset();
    }
  }

  // Pan: middle drag, right drag, Space + left drag, or the hand tool.
  const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space) && _hovered && !io.WantTextInput;
  const bool panButton = ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                         (ImGui::IsMouseDragging(ImGuiMouseButton_Right) && !isTileTool(editor.tool())) ||
                         ((spaceHeld || editor.tool() == Tool::Pan) && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
  if (ImGui::IsItemActive() && panButton && (_drag == Drag::None || _drag == Drag::Pan)) {
    _drag = Drag::Pan;
    _center -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / _zoom;
    _targetCenter.reset();
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  }
  if (spaceHeld && _drag == Drag::None) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  // Arrow keys nudge the selection (Shift: a grid step).
  if (ImGui::IsWindowFocused() && !io.WantTextInput && !editor.selection().empty() && _drag == Drag::None) {
    glm::vec2 nudge(0.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= 1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += 1;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y += 1;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y -= 1;
    if (nudge != glm::vec2(0.0f)) {
      nudge *= io.KeyShift ? _gridSize : 1.0f;
      scene.editEntities(editor.selection(), "Nudge", [&](Json& e) {
        const Json position = fieldValue(project, e, "TransformComponent", "position");
        editableComponent(e, "TransformComponent")["position"] = movedBy(position, nudge);
      }, "nudge");
    }
  }

  if (isTileTool(editor.tool()) || editor.tool() == Tool::Pan || spaceHeld) return;

  // Double-clicking a part of the game's UI opens its screen in the UI editor, at that element.
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && _showUi && openUiAt(editor, world)) {
    _drag = Drag::None;
    return;
  }

  // Press: a gizmo handle, an entity, or empty space.
  // A shape handle under the click takes it before the gizmo and picking.
  const bool shapeClick = ImGui::IsItemClicked(ImGuiMouseButton_Left) && _drag == Drag::None && !isTileTool(editor.tool()) &&
                          startShapeDrag(editor, mouse);
  if (shapeClick && _shape) {
    _drag = Drag::Shape;
    gestureKey("scene-shape", true);
  }
  if (_drag == Drag::Shape && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) applyShapeDrag(editor, world);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && _drag == Drag::None && !shapeClick) {
    _dragStart = world;
    _dragOriginals.clear();
    _dragFrames.clear();
    Drag start = Drag::None;
    if (auto b = editor.selection().empty() ? std::nullopt : preview.bounds(editor.primary())) {
      std::array<ImVec2, 4> corners;
      for (int i = 0; i < 4; ++i) corners[i] = toScreen(b->corners[i]);
      const GizmoHit hit = gizmoHit(editor.tool(), toScreen(b->position), corners, mouse);
      start = hit.free ? Drag::Move : hit.x ? Drag::MoveX : hit.y ? Drag::MoveY : hit.ring ? Drag::Rotate
              : hit.corners ? Drag::Scale : Drag::None;
    }
    if (start == Drag::None) {
      const auto hits = preview.pick(world);
      if (hits.empty()) {
        if (!io.KeyShift && !io.KeyCtrl) editor.clearSelection();
        start = Drag::Box;
      } else {
        // Alt-click cycles through what's stacked under the cursor.
        EntityUid hit = hits.front();
        if (io.KeyAlt && editor.isSelected(hits.front()) && hits.size() > 1) {
          auto it = std::find(hits.begin(), hits.end(), editor.primary());
          hit = (it == hits.end() || it + 1 == hits.end()) ? hits.front() : *(it + 1);
        }
        if (io.KeyCtrl) editor.select(hit, Editor::SelectMode::Toggle);
        else if (io.KeyShift) editor.select(hit, Editor::SelectMode::Add);
        else if (!editor.isSelected(hit) || io.KeyAlt) editor.select(hit);
        else editor.selectAll([&] {  // keep the group, make the clicked one primary
          auto sel = editor.selection();
          std::erase(sel, hit);
          sel.insert(sel.begin(), hit);
          return sel;
        }());
        // Dragging an entity's body moves it (with the Select and Move tools).
        if (editor.isSelected(hit) && (editor.tool() == Tool::Move || editor.tool() == Tool::Select)) start = Drag::Move;
      }
    }
    if (start != Drag::None && start != Drag::Box) {
      _dragFrames.clear();
      // Entities inside other selected ones ride along with them.
      for (EntityUid uid : editor.selectionRoots()) {
        const Json* e = scene.find(uid);
        if (!e) continue;
        _dragOriginals[uid] = effectiveComponents(project, *e).value("TransformComponent", Json::object());
        DragFrame frame{xyOf(_dragOriginals[uid].value("position", Json()), glm::vec2(0.0f)), 0.0f};
        if (auto t = editor.worldTransform(uid)) frame.world = glm::vec2(t->position);
        if (auto p = editor.worldTransform(scene.parentOf(uid))) frame.parentTurn = p->rotationRad;
        _dragFrames[uid] = frame;
      }
    }
    _drag = start;
    gestureKey("scene-drag", true);
  }

  if (_drag == Drag::Box && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (glm::length(world - _dragStart) * _zoom > 3.0f) {
      auto inBox = preview.pickRect(_dragStart, world);
      if (io.KeyShift || io.KeyCtrl) {
        for (EntityUid uid : inBox) editor.select(uid, Editor::SelectMode::Add);
      } else {
        editor.selectAll(inBox);
      }
    }
    _drag = Drag::None;
  }

  const bool transformDrag = _drag == Drag::Move || _drag == Drag::MoveX || _drag == Drag::MoveY || _drag == Drag::Rotate ||
                             _drag == Drag::Scale;
  if (transformDrag) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      // A click without movement selects; only real drags edit.
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) applyTransformDrag(editor, world, io.KeyShift);
      ImGui::SetMouseCursor(_drag == Drag::Rotate ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeAll);
    } else {
      _drag = Drag::None;
    }
  }

  // Right-click on empty view (no pan): a menu to create things here.
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && _hovered && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0 &&
      ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).y == 0) {
    const auto hits = preview.pick(world);
    if (!hits.empty() && !editor.isSelected(hits.front())) editor.select(hits.front());
    _dragStart = snapped(world, false);
    ImGui::OpenPopup("scene context");
  }
  if (ImGui::BeginPopup("scene context")) {
    if (!editor.selection().empty()) {
      for (const char* id : {"view.frame", "edit.duplicate", "edit.rename", "edit.delete"}) editor.commands().menuItem(id);
      ImGui::Separator();
    }
    if (ImGui::BeginMenu(ICON_PLUS "  Create Here")) {
      for (const auto& [kind, icon] : std::vector<std::pair<const char*, const char*>>{
               {"Empty", ICON_CUBE_TRANSPARENT}, {"Sprite", ICON_IMAGE}, {"Text", ICON_TEXT_T}, {"Tile Map", ICON_GRID_FOUR},
               {"UI Screen", ICON_BROWSER}, {"Sound", ICON_SPEAKER_HIGH}, {"Script", ICON_CODE}}) {
        const std::string label = std::string(icon) + "  " + kind;
        if (ImGui::MenuItem(label.c_str())) editor.createEntity(kind, _dragStart);
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem(ICON_LINK "  Copy Spot Reference")) {  // for an agent: the spot, and whose ground it's on
      std::string text = scene.spotReference(_dragStart);
      if (auto ground = preview.terrainAt(_dragStart, 8.0f / _zoom)) text += " (on the ground of " + scene.reference(*ground) + ")";
      ImGui::SetClipboardText(text.c_str());
      editor.toasts().show(Toasts::Kind::Info, "Copied", text);
    }
    editor.commands().menuItem("view.frameAll");
    ImGui::EndPopup();
  }
}

void ScenePanel::applyTransformDrag(Editor& editor, glm::vec2 world, bool fine) {
  SceneDocument& scene = *editor.scene();
  // What was selected when the drag began moves, from where it was then; the rest of the selection doesn't.
  // The primary leads (it may be inside another dragged entity, which then carries it).
  EntityUid lead = editor.primary();
  for (EntityUid up = lead; up; up = scene.parentOf(up)) {
    if (_dragOriginals.contains(up)) lead = up;
  }
  const auto primary = _dragOriginals.find(lead);
  const int primaryIndex = scene.indexOf(lead);
  if (primary == _dragOriginals.end() || primaryIndex < 0) return;
  std::vector<EntityUid> dragged;
  for (const auto& [uid, transform] : _dragOriginals) dragged.push_back(uid);
  const auto originalOf = [&](const Json& e) -> const Json& { return _dragOriginals.at(e.value(kUidKey, EntityUid{0})); };
  const auto frameOf = [&](const Json& e) { return _dragFrames[e.value(kUidKey, EntityUid{0})]; };
  const glm::vec2 primaryStart = _dragFrames[lead].world;  // pivots and snapping work in the world
  const bool snap = _snap != ImGui::GetIO().KeyCtrl;
  const std::string key = gestureKey("scene-drag", false);

  switch (_drag) {
    case Drag::Move:
    case Drag::MoveX:
    case Drag::MoveY: {
      // Snap the primary's new position (to the grid, else to whole pixels); everything moves by the same amount.
      const glm::vec2 axes = _drag == Drag::MoveX ? glm::vec2(1, 0) : _drag == Drag::MoveY ? glm::vec2(0, 1) : glm::vec2(1);
      const glm::vec2 delta = (glm::round(snapped(primaryStart + (world - _dragStart) * axes, false)) - primaryStart) * axes;
      const std::string label = dragged.size() == 1 ? "Move " + scene.displayName(static_cast<size_t>(primaryIndex)) : "Move";
      scene.editEntities(dragged, label, [&](Json& e) {
        // A child moves in its parent's frame: the world move, turned back by the parent's rotation.
        const float turn = -frameOf(e).parentTurn;
        const glm::vec2 own = glm::vec2(std::cos(turn) * delta.x - std::sin(turn) * delta.y, std::sin(turn) * delta.x + std::cos(turn) * delta.y);
        editableComponent(e, "TransformComponent")["position"] = movedBy(originalOf(e).value("position", Json()), turn == 0.0f ? delta : glm::round(own * 1000.0f) / 1000.0f);
      }, key);
      break;
    }
    case Drag::Rotate: {
      const float delta = std::atan2(world.y - primaryStart.y, world.x - primaryStart.x) -
                          std::atan2(_dragStart.y - primaryStart.y, _dragStart.x - primaryStart.x);
      scene.editEntities(dragged, "Rotate", [&](Json& e) {
        float r = originalOf(e).value("rotation", 0.0f) + delta;
        if (snap) r = std::round(r / (glm::pi<float>() / 12)) * (glm::pi<float>() / 12);  // 15° steps
        editableComponent(e, "TransformComponent")["rotation"] = r;
      }, key);
      break;
    }
    case Drag::Scale: {
      // Scale by how far the cursor moved from the pivot, per axis (Shift: uniform).
      glm::vec2 factor = glm::abs(world - primaryStart) / glm::max(glm::abs(_dragStart - primaryStart), glm::vec2(1.0f));
      if (fine) factor = glm::vec2(std::max(factor.x, factor.y));
      scene.editEntities(dragged, "Scale", [&](Json& e) {
        glm::vec2 s = xyOf(originalOf(e).value("scale", Json()), glm::vec2(1.0f)) * factor;
        if (snap) s = glm::max(glm::round(s), glm::vec2(1.0f));
        editableComponent(e, "TransformComponent")["scale"] = {s.x, s.y};
      }, key);
      break;
    }
    default:
      break;
  }
}

void ScenePanel::handleTilePainting(Editor& editor) {
  const EntityUid uid = editor.primary();
  const std::string path = editor.mapPathOf(uid);
  const Json* map = path.empty() ? nullptr : editor.map(path);
  glm::vec2 origin(0.0f);
  if (!map || !editor.preview().tileGrid(uid, &origin) || !tiled::uneditable(*map).empty()) return;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const glm::vec2 ts(tiled::tileSize(*map));
  const glm::ivec2 size(tiled::width(*map), tiled::height(*map));
  const glm::ivec2 cell(glm::floor((_cursorWorld - origin) / ts));
  const Json& layers = (*map)["layers"];
  int& layer = editor.activeLayer(path);
  layer = std::clamp(layer, 0, std::max(0, static_cast<int>(layers.size()) - 1));

  // The map's outline and, when hovered, the cell under the cursor.
  draw->AddRect(toScreen(origin + glm::vec2(0, size.y * ts.y)), toScreen(origin + glm::vec2(size.x * ts.x, 0)), theme::u32(theme::accent, 0.5f));
  auto cellRect = [&](glm::ivec2 c, ImU32 color, bool filled) {
    const ImVec2 a = toScreen(origin + glm::vec2(c) * ts + glm::vec2(0, ts.y)), b = toScreen(origin + glm::vec2(c + 1) * ts - glm::vec2(0, ts.y));
    if (filled) draw->AddRectFilled(a, b, color);
    else draw->AddRect(a, b, color, 0.0f, 1.5f);
  };
  if (layers.empty()) return;
  if (tiled::isObjectLayer(layers[static_cast<size_t>(layer)])) {
    handleMapObjects(editor, path, origin);
    return;
  }
  if (!tiled::isTileLayer(layers[static_cast<size_t>(layer)])) return;

  // The brush in this map: its tile's gid, or its terrain.
  Editor::TileBrush& brush = editor.tileBrush();
  uint32_t first = 0;
  for (const tiled::TilesetRef& ref : tiled::tilesets(*map, path)) {
    if (ref.path == brush.tileset) first = ref.firstGid;
  }
  const Json* tileset = first ? editor.tileset(brush.tileset) : nullptr;
  const bool terrain = tileset && brush.terrainSet >= 0;
  const uint32_t gid = first && !terrain ? (first + brush.tile) | brush.flips : 0;
  const Tool tool = editor.tool();
  const bool erasing = tool == Tool::TileErase || (ImGui::IsMouseDown(ImGuiMouseButton_Right) && tool == Tool::TileBrush);
  const bool inMap = cell.x >= 0 && cell.y >= 0 && cell.x < size.x && cell.y < size.y;
  const bool hoveredCell = _hovered && inMap;
  // Paints one cell of `m`: the tile, the terrain, or nothing.
  auto paintCell = [&](Json& m, glm::ivec2 c) {
    if (terrain) return tiled::paintTerrain(m, layer, c, *tileset, first, brush.terrainSet, erasing ? 0 : brush.terrainColor);
    return tiled::setGid(m, layer, c, erasing ? 0 : gid);
  };
  const bool canPaint = erasing || gid || terrain;

  // A press picks, floods, or starts a stroke.
  const bool press = ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemClicked(ImGuiMouseButton_Right) && tool == Tool::TileBrush);
  if (press && hoveredCell && !ImGui::IsKeyDown(ImGuiKey_Space) && _drag == Drag::None) {
    if (tool == Tool::TilePick) {
      // The topmost tile there, from any layer: the brush takes it, and its layer becomes the one painted.
      for (int l = static_cast<int>(layers.size()) - 1; l >= 0; --l) {
        const uint32_t picked = tiled::gidAt(*map, l, cell);
        if (!picked) continue;
        for (const tiled::TilesetRef& ref : tiled::tilesets(*map, path)) {
          if ((picked & ~tiled::kFlags) >= ref.firstGid) brush = {ref.path, (picked & ~tiled::kFlags) - ref.firstGid, picked & tiled::kFlags};
        }
        layer = l;
        break;
      }
      editor.setTool(Tool::TileBrush);
    } else if (tool == Tool::TileFill && canPaint) {
      editor.editMap(path, "Fill Tiles", [&](Json& m) {
        if (!terrain) {
          tiled::fill(m, layer, cell, erasing ? 0 : gid);
          return;
        }
        // A terrain fill paints every cell of the region.
        const uint32_t from = tiled::gidAt(m, layer, cell);
        Json region = m;
        tiled::fill(region, layer, cell, ~0u);
        for (int y = 0; y < size.y; ++y) {
          for (int x = 0; x < size.x; ++x) {
            if (tiled::gidAt(region, layer, {x, y}) == ~0u && tiled::gidAt(m, layer, {x, y}) == from) paintCell(m, {x, y});
          }
        }
      });
    } else if (canPaint) {
      _drag = Drag::Paint;
      _lastPaintCell.reset();
      _dragStart = glm::vec2(cell);
      gestureKey("paint", true);
    }
  }

  if (_drag == Drag::Paint) {
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (tool == Tool::TileRect) {
      // Preview the rectangle; fill it on release.
      const glm::ivec2 a = glm::ivec2(_dragStart), lo = glm::max(glm::min(a, cell), glm::ivec2(0)), hi = glm::min(glm::max(a, cell), size - 1);
      for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) cellRect({x, y}, theme::u32(erasing ? theme::error : theme::accent, 0.25f), true);
      }
      if (!down) {
        editor.editMap(path, erasing ? "Erase Rectangle" : "Fill Rectangle", [&](Json& m) {
          for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) paintCell(m, {x, y});
          }
        });
      }
    } else if (down && (tool == Tool::TileBrush || tool == Tool::TileErase)) {
      // Every cell between the last and this one, so fast strokes leave no gaps.
      const glm::ivec2 from = _lastPaintCell.value_or(cell);
      if (from != cell || !_lastPaintCell) {
        const int steps = std::max(std::abs(cell.x - from.x), std::abs(cell.y - from.y));
        editor.editMap(path, erasing ? "Erase Tiles" : "Paint Tiles", [&](Json& m) {
          for (int i = 0; i <= steps; ++i) {
            const float t = steps == 0 ? 0.0f : static_cast<float>(i) / steps;
            paintCell(m, glm::ivec2(glm::round(glm::mix(glm::vec2(from), glm::vec2(cell), t))));
          }
        }, gestureKey("paint", false));
      }
      _lastPaintCell = cell;
    }
    if (!down) _drag = Drag::None;
  }

  if (!hoveredCell) return;
  // A ghost of the tile about to land, then the cell's outline.
  if (gid && !erasing && tool != Tool::TilePick && tileset) {
    if (auto p = widgets::tilePicture(*editor.project(), *tileset, brush.tileset, brush.tile)) {
      const ImVec2 a = toScreen(origin + glm::vec2(cell) * ts + glm::vec2(0, p->size.y)), b = toScreen(origin + glm::vec2(cell) * ts + glm::vec2(p->size.x, 0));
      const auto uv = flippedUvs(*p, brush.flips);
      draw->AddImageQuad(p->texture, a, {b.x, a.y}, b, {a.x, b.y}, uv[0], uv[1], uv[2], uv[3], IM_COL32(255, 255, 255, 150));
    }
  }
  cellRect(cell, theme::u32(erasing ? theme::error : theme::accent, 0.9f), false);
  ImGui::SetMouseCursor(tool == Tool::TilePick ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_Arrow);
  // What's there: the topmost tile's type, on which layer.
  std::string here = "empty";
  for (int l = static_cast<int>(layers.size()) - 1; l >= 0; --l) {
    const uint32_t g = tiled::gidAt(*map, l, cell) & ~tiled::kFlags;
    if (!g) continue;
    for (const tiled::TilesetRef& ref : tiled::tilesets(*map, path)) {
      if (g < ref.firstGid) continue;
      const Json* set = editor.tileset(ref.path);
      const std::string type = set ? tiled::typeOf(*set, g - ref.firstGid) : "";
      here = (type.empty() ? "tile #" + std::to_string(g - ref.firstGid) : type) + "  (" + layers[static_cast<size_t>(l)].value("name", std::string()) + ")";
    }
    break;
  }
  ImGui::BeginTooltip();
  ImGui::Text("%d, %d   %s", cell.x, cell.y, here.c_str());
  ImGui::EndTooltip();
}

void ScenePanel::handleMapObjects(Editor& editor, const std::string& path, glm::vec2 origin) {
  const Json& map = *editor.map(path);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  int& selected = editor.selectedObject();
  const int layer = editor.activeLayer(path);
  const glm::vec2 local = _cursorWorld - origin;
  const glm::vec2 ts(tiled::tileSize(map));
  // Whole tiles unless Alt is held.
  auto snap = [&](glm::vec2 p) { return ImGui::GetIO().KeyAlt ? glm::round(p) : glm::round(p / ts) * ts; };

  // Every visible object, the active layer's brighter, the selected one accented.
  for (size_t l = 0; l < map["layers"].size(); ++l) {
    const Json& objects = map["layers"][l];
    if (!tiled::isObjectLayer(objects) || !objects.value("visible", true)) continue;
    const bool active = static_cast<int>(l) == layer;
    for (const Json& o : objects["objects"]) {
      const glm::vec4 r = tiled::objectRect(map, o);
      const bool isSelected = o.value("id", 0) == selected;
      const ImU32 color = theme::u32(isSelected ? theme::accentBright : theme::info, active ? 0.95f : 0.4f);
      const ImVec2 a = toScreen(origin + glm::vec2(r.x, r.y + r.w)), b = toScreen(origin + glm::vec2(r.x + r.z, r.y));
      if (r.z <= 0 || r.w <= 0) {
        draw->AddCircle(a, 6.0f, color, 0, 2.0f);
      } else {
        draw->AddRectFilled(a, b, theme::u32(isSelected ? theme::accent : theme::info, active ? 0.18f : 0.06f));
        draw->AddRect(a, b, color, 0.0f, isSelected ? 2.0f : 1.0f);
      }
      std::string label = o.value("name", std::string());
      if (const std::string type = o.value("type", o.value("class", std::string())); !type.empty()) label += label.empty() ? type : "  (" + type + ")";
      if (!label.empty()) draw->AddText({a.x + 3, a.y + 2}, color, label.c_str());
    }
  }

  const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsKeyDown(ImGuiKey_Space);
  if (clicked && _drag == Drag::None) {
    const int hit = tiled::objectAt(map, local);
    int hitLayer = -1;
    Json copy = map;
    tiled::findObject(copy, hit, &hitLayer);
    _drag = Drag::Object;
    if (hit && hitLayer == layer) {  // move it
      selected = _movingObject = hit;
      _dragStart = local;
      _movingFrom = tiled::objectRect(map, *tiled::findObject(copy, hit));
      gestureKey("object", true);
    } else {  // draw a new one
      selected = _movingObject = 0;
      _dragStart = snap(local);
    }
  }
  if (_drag == Drag::Object) {
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (_movingObject) {
      const glm::vec2 at = snap(glm::vec2(_movingFrom) + local - _dragStart);
      const int id = _movingObject;
      editor.editMap(path, "Move Object", [&](Json& m) {
        if (Json* o = tiled::findObject(m, id)) tiled::setObjectRect(m, *o, {at, _movingFrom.z, _movingFrom.w});
      }, gestureKey("object", false));
    } else {
      const glm::vec2 a = glm::min(_dragStart, snap(local)), b = glm::max(_dragStart, snap(local));
      draw->AddRect(toScreen(origin + glm::vec2(a.x, b.y)), toScreen(origin + glm::vec2(b.x, a.y)), theme::u32(theme::accent), 0.0f, 1.5f);
      if (!down && b.x > a.x && b.y > a.y) {
        int made = 0;
        editor.editMap(path, "Add Object", [&](Json& m) { made = tiled::addObject(m, layer, {a, b - a}, ""); });
        selected = made;
      }
    }
    if (!down) _drag = Drag::None;
  }
  if (selected && _hovered && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
    const int id = selected;
    editor.editMap(path, "Delete Object", [&](Json& m) { tiled::removeObject(m, id); });
    selected = 0;
  }
  if (_hovered) ImGui::SetMouseCursor(tiled::objectAt(map, local) ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_Arrow);
}

void ScenePanel::drawTilePalette(Editor& editor) {
  const std::string path = editor.mapPathOf(editor.primary());
  const Json* map = path.empty() ? nullptr : editor.map(path);
  if (!map) return;
  const Project& project = *editor.project();
  Editor::TileBrush& brush = editor.tileBrush();
  const Json& layers = (*map)["layers"];
  int& layer = editor.activeLayer(path);
  const auto refs = tiled::tilesets(*map, path);
  const std::string problem = tiled::uneditable(*map);
  const bool objects = problem.empty() && !layers.empty() && tiled::isObjectLayer(layers[static_cast<size_t>(std::clamp(layer, 0, static_cast<int>(layers.size()) - 1))]);

  const float pad = 8.0f, cell = 36.0f;  // 16 px tiles show at 2x
  const int columns = 8;
  const float width = columns * (cell + 3) + pad * 2;
  // The brush's tileset, else the map's first.
  std::string shown = brush.tileset;
  if (std::none_of(refs.begin(), refs.end(), [&](const tiled::TilesetRef& r) { return r.path == shown; })) shown = refs.empty() ? "" : refs.front().path;
  const Json* tileset = shown.empty() ? nullptr : editor.tileset(shown);
  const auto ids = tileset ? tiled::tileIds(*tileset) : std::vector<uint32_t>{};
  const int rows = std::clamp(static_cast<int>((ids.size() + columns - 1) / columns), 1, 5);
  const bool terrains = tileset && tileset->contains("wangsets");
  // Anchored to the view's bottom-left by its height, as measured last frame.
  ImGui::SetCursorScreenPos({_origin.x + 12, _origin.y + _size.y - _paletteHeight - 12});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::withAlpha(theme::bg2, 0.96f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::radiusOverlay);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
  ImGui::BeginChild("##palette", {width, 0}, ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);

  // The layer painted on.
  ImGui::AlignTextToFramePadding();
  ui::smallText("Layer", theme::textDim);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  const std::string current = layers.empty() ? "none" : layers[static_cast<size_t>(std::clamp(layer, 0, static_cast<int>(layers.size()) - 1))].value("name", std::string());
  if (ui::beginCombo("##layer", current.c_str())) {
    for (int l = static_cast<int>(layers.size()) - 1; l >= 0; --l) {
      const Json& entry = layers[static_cast<size_t>(l)];
      if (!tiled::isTileLayer(entry) && !tiled::isObjectLayer(entry)) continue;
      const std::string label = std::string(tiled::isObjectLayer(entry) ? ICON_SHAPES "  " : ICON_GRID_FOUR "  ") + entry.value("name", std::string());
      if (ImGui::Selectable(label.c_str(), l == layer)) layer = l, editor.selectedObject() = 0;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ui::iconButton("tiled", ICON_ARROW_SQUARE_OUT, "Open the map in Tiled")) editor.openInTiled(path);
  if (problem.empty() && !objects && !refs.empty()) {
    // Flips for the tiles painted next.
    ImGui::SameLine(width - pad * 2 - 3 * (ImGui::GetFrameHeight() + 2));
    if (ui::iconButton("flipH", ICON_ARROWS_LEFT_RIGHT, "Flip horizontally", brush.flips & tiled::kFlipH)) brush.flips ^= tiled::kFlipH;
    ImGui::SameLine(0, 2);
    if (ui::iconButton("flipV", ICON_ARROWS_DOWN_UP, "Flip vertically", brush.flips & tiled::kFlipV)) brush.flips ^= tiled::kFlipV;
    ImGui::SameLine(0, 2);
    if (ui::iconButton("rotate", ICON_ARROW_CLOCKWISE, "Rotate a quarter turn")) {
      // Clockwise in Tiled's flags: what was flipped vertically flips horizontally, and the diagonal toggles.
      const bool h = brush.flips & tiled::kFlipH, v = brush.flips & tiled::kFlipV, d = brush.flips & tiled::kFlipD;
      brush.flips = (v ? 0 : tiled::kFlipH) | (h ? tiled::kFlipV : 0) | (d ? 0 : tiled::kFlipD);
    }
  }

  if (!problem.empty()) {
    ImGui::TextWrapped("%s", problem.c_str());
  } else if (objects) {
    ui::smallText("Drag to draw an object; drag one to move it.", theme::textFaint);
    ui::smallText("Alt: off the grid.  Del: remove.  Inspector: name, type, properties.", theme::textFaint);
  } else if (refs.empty()) {
    ui::smallText("This map has no tileset yet: add one in the Inspector.", theme::textFaint);
  } else {
    // Tileset tabs.
    if (refs.size() > 1) {
      for (const tiled::TilesetRef& ref : refs) {
        if (ref.path.empty()) continue;
        if (ui::chip(ref.path.c_str(), std::filesystem::path(ref.path).stem().string().c_str()) ) brush = {ref.path, 0};
        if (ref.path == shown) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme::u32(theme::accent), theme::radius, 1.5f);
        ImGui::SameLine(0, 4);
      }
      ImGui::NewLine();
    }
    // The tiles.
    ImGui::BeginChild("##tiles", {0, rows * (cell + 3)}, 0);
    int n = 0;
    for (uint32_t id : ids) {
      if (n++ % columns) ImGui::SameLine(0, 3);
      ImGui::PushID(static_cast<int>(id));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##tile", {cell, cell})) {
        brush = {shown, id, brush.flips};
        if (editor.tool() == Tool::TileErase || editor.tool() == Tool::TilePick) editor.setTool(Tool::TileBrush);
      }
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(p, {p.x + cell, p.y + cell}, theme::u32(hovered ? theme::bg4 : theme::bg1), theme::radius);
      if (auto pic = widgets::tilePicture(project, *tileset, shown, id)) widgets::fitted(draw, *pic, {p.x + 2, p.y + 2}, {p.x + cell - 2, p.y + cell - 2});
      if (brush.tileset == shown && brush.tile == id && brush.terrainSet < 0) draw->AddRect(p, {p.x + cell, p.y + cell}, theme::u32(theme::accent), theme::radius, 2.0f);
      if (hovered) {
        const std::string type = tiled::typeOf(*tileset, id);
        const Json* entry = tiled::tileEntry(*tileset, id);
        std::string tip = type.empty() ? "tile #" + std::to_string(id) : type;
        if (entry && tiled::property(*entry, "solid") == Json(true)) tip += "  solid";
        ui::tooltip(tip.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    // Terrains: paint by kind, the tiles choosing themselves.
    if (terrains) {
      const Json& sets = (*tileset)["wangsets"];
      for (size_t s = 0; s < sets.size(); ++s) {
        const Json colors = sets[s].value("colors", Json::array());
        for (size_t c = 0; c < colors.size(); ++c) {
          ImGui::PushID(static_cast<int>(s * 64 + c));
          const std::string label = colors.size() > 1 ? sets[s].value("name", std::string()) + ": " + colors[c].value("name", std::string())
                                                       : sets[s].value("name", std::string());
          const bool on = brush.tileset == shown && brush.terrainSet == static_cast<int>(s) && brush.terrainColor == static_cast<int>(c) + 1;
          if (ui::chip("terrain", (std::string(ICON_MOUNTAINS "  ") + label).c_str())) {
            brush = {shown, brush.tile, 0, static_cast<int>(s), static_cast<int>(c) + 1};
            if (editor.tool() == Tool::TileErase || editor.tool() == Tool::TilePick) editor.setTool(Tool::TileBrush);
          }
          if (on) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme::u32(theme::accent), theme::radius, 1.5f);
          ui::tooltip("Terrain brush: paints this terrain and picks the tiles that join up");
          ImGui::SameLine(0, 4);
          ImGui::PopID();
        }
      }
      ImGui::NewLine();
    }
    ui::smallText("B brush  U rect  G fill  X erase  I pick", theme::textFaint);
  }
  ImGui::EndChild();
  _paletteHeight = ImGui::GetItemRectSize().y;
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void ScenePanel::drawOverlayToolbar(Editor& editor) {
  // Floating view options in the top-right corner.
  const float h = 26.0f;
  const ImVec2 at{_origin.x + _size.x - 8, _origin.y + 8};
  const float width = 6 * (h + 2) + 64 + 10;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled({at.x - width - 6, at.y - 3}, {at.x + 3, at.y + h + 3}, theme::u32(theme::bg1, 0.92f), theme::radius + 2);
  ImGui::SetCursorScreenPos({at.x - width, at.y});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, 0});
  if (ui::iconButton("grid", ICON_GRID_NINE, "Grid", _showGrid, ImGuiMod_Ctrl | ImGuiKey_Apostrophe, h)) _showGrid = !_showGrid;
  ImGui::SameLine();
  if (ui::iconButton("snap", ICON_MAGNET, "Snap to grid (hold Ctrl to flip)", _snap, ImGuiMod_Shift | ImGuiKey_G, h)) _snap = !_snap;
  ImGui::SameLine();
  if (ui::iconButton("gridsize", ICON_RULER, "Grid size", false, 0, h)) ImGui::OpenPopup("grid size");
  if (ImGui::BeginPopup("grid size")) {
    ui::sectionLabel("Grid size");
    for (float g : {4.0f, 8.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f}) {
      char item[16];
      std::snprintf(item, sizeof(item), "%.0f px", g);
      if (ImGui::MenuItem(item, nullptr, _gridSize == g)) _gridSize = g;
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ui::iconButton("colliders", ICON_BOUNDING_BOX, "Colliders and terrain", _showColliders, 0, h)) _showColliders = !_showColliders;
  ImGui::SameLine();
  if (ui::iconButton("frame", ICON_MONITOR, "Game frame", _showGameFrame, 0, h)) _showGameFrame = !_showGameFrame;
  ImGui::SameLine();
  if (ui::iconButton("ui", ICON_BROWSER, "Game UI (screens laid out in the game frame)", _showUi, 0, h)) _showUi = !_showUi;
  ImGui::SameLine(0, 6);
  char zoom[16];
  std::snprintf(zoom, sizeof(zoom), "%d%%", static_cast<int>(std::round(_zoom * 100)));
  ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(theme::bg1, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 4});
  if (ImGui::Button(zoom, {64, h})) ImGui::OpenPopup("zoom");
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ui::tooltip("Zoom");
  if (ImGui::BeginPopup("zoom")) {
    editor.commands().menuItem("view.frameAll");
    editor.commands().menuItem("view.frame");
    ImGui::Separator();
    for (float z : {0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 8.0f}) {
      char item[16];
      std::snprintf(item, sizeof(item), "%d%%", static_cast<int>(z * 100));
      if (ImGui::MenuItem(item, z == 1.0f ? shortcutLabel(ImGuiMod_Ctrl | ImGuiKey_0).c_str() : nullptr)) setZoom(z);
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

void ScenePanel::drawDropTarget(Editor& editor) {
  if (!ImGui::BeginDragDropTarget()) return;
  if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("JM_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                 ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    const std::string path(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
    const glm::vec2 mouse = toWorld(ImGui::GetMousePos()), at = snapped(mouse, false);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    // Over an entity that can take it (a script, a picture...) it applies there; else a ghost of what will land, at its real size.
    const auto hits = editor.preview().pick(mouse);
    const EntityUid target = !hits.empty() && editor.applyAssetToEntity(hits.front(), path, true) ? hits.front() : 0;
    const ImVec2 c = toScreen(at);
    if (target) {
      if (auto b = editor.preview().bounds(target)) {
        ImVec2 pts[4];
        for (int i = 0; i < 4; ++i) pts[i] = toScreen(b->corners[i]);
        draw->AddPolyline(pts, 4, theme::u32(theme::accent), 2.5f, ImDrawFlags_Closed);
      }
    } else if (auto picture = Thumbnails::instance().get(*editor.project(), path)) {
      const ImVec2 half{picture->size.x * 0.5f * _zoom, picture->size.y * 0.5f * _zoom};
      draw->AddImage(picture->texture, {c.x - half.x, c.y - half.y}, {c.x + half.x, c.y + half.y}, picture->uv0, picture->uv1,
                     theme::u32(theme::text, 0.7f));
      draw->AddRect({c.x - half.x, c.y - half.y}, {c.x + half.x, c.y + half.y}, theme::u32(theme::accent), 0.0f, 1.0f);
    } else {
      draw->AddCircleFilled(c, 5.0f, theme::u32(theme::accent));
    }
    if (payload->IsDelivery()) {
      if (target) editor.applyAssetToEntity(target, path);
      else editor.instantiateAsset(path, at);
      ImGui::SetWindowFocus();
    }
  }
  ImGui::EndDragDropTarget();
}

bool ScenePanel::openUiAt(Editor& editor, glm::vec2 world) {
  SceneDocument* scene = editor.scene();
  HostedEngine* engine = editor.preview().engine();
  UIModule* ui = engine ? engine->engine().getModules().find<UIModule>() : nullptr;
  if (!scene || !ui) return false;
  // The UI is laid out in the game's frame, centered on the world origin, y down.
  const glm::vec2 game(editor.preview().gameSize());
  const glm::vec2 point{world.x + game.x * 0.5f, game.y * 0.5f - world.y};
  for (size_t i = scene->size(); i-- > 0;) {  // later screens draw on top
    const Json components = effectiveComponents(*editor.project(), scene->entity(i));
    const std::string src = components.value("UIDocumentComponent", Json::object()).value("src", std::string());
    const auto id = editor.preview().entityOf(scene->uid(i));
    const LayoutBox* root = src.empty() || !id ? nullptr : ui->layoutOfEntity(*id);
    if (!root) continue;
    // The deepest box under the point that isn't a whole-screen container.
    const LayoutBox* hit = nullptr;
    std::vector<const LayoutBox*> stack{root};
    while (!stack.empty()) {
      const LayoutBox* b = stack.back();
      stack.pop_back();
      const glm::vec4 r = b->rect;
      if (point.x < r.x || point.y < r.y || point.x >= r.x + r.z || point.y >= r.y + r.w) continue;
      if (b->node && !b->node->isText() && b->node->parent && b->style.visible && r.z * r.w < game.x * game.y * 0.9f) hit = b;
      for (const auto& c : b->children) stack.push_back(c.get());
    }
    if (!hit) continue;
    std::string path;
    for (int index : uisource::pathOf(*hit->node)) path += (path.empty() ? "" : "/") + std::to_string(index);
    editor.select(scene->uid(i));
    editor.openAsset(src, path);
    return true;
  }
  return false;
}
