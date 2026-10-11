#include "DebugDraw.hpp"

#include <cmath>
#include <numbers>

#include <nlohmann/json.hpp>

#include "../renderer2d/Renderer2D.hpp"

namespace {

constexpr size_t kMaxItems = 10000;  // a runaway loop of timed items can't eat memory
constexpr float kWidth = 1.5f;       // logical px

bool finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

double tidy(float value) { return std::round(static_cast<double>(value) * 100.0) / 100.0; }
nlohmann::json tidy(glm::vec2 v) { return {tidy(v.x), tidy(v.y)}; }

}  // namespace

void DebugDraw::add(Shape shape, bool screen, glm::vec2 a, glm::vec2 b, uint32_t color, float seconds,
                    bool redrawnWhilePaused, std::string text) {
  const bool known = shape >= Shape::Line && shape <= Shape::Text;
  if (!known || !finite(a) || !finite(b) || !std::isfinite(seconds) || _items.size() >= kMaxItems) return;
  const glm::vec4 rgba(static_cast<float>((color >> 16) & 0xFF) / 255.0f, static_cast<float>((color >> 8) & 0xFF) / 255.0f,
                       static_cast<float>(color & 0xFF) / 255.0f, 1.0f);
  _items.push_back({shape, screen, a, b, rgba, std::move(text), std::max(seconds, 0.0f),
                    redrawnWhilePaused});
}

void DebugDraw::expire(float dt, bool paused) {
  std::erase_if(_items, [&](Item& item) {
    if (item.seconds <= 0.0f) return !paused || item.redrawnWhilePaused;
    item.seconds -= dt;
    return item.seconds <= 0.0f;
  });
}

glm::vec2 DebugDraw::toScreen(Renderer2D& renderer, glm::vec2 world) {
  const Camera2D& camera = renderer.camera();
  return glm::vec2(renderer.logicalSize()) * 0.5f + glm::vec2(1.0f, -1.0f) * (world - camera.position()) * camera.zoom();
}

void DebugDraw::drawShapes(Renderer2D& renderer, bool screen) const {
  const auto line = [&](const Item& item, glm::vec2 p, glm::vec2 q) {
    if (item.screen) renderer.drawScreenLine(p, q, item.color, kWidth);
    else renderer.drawScreenLine(toScreen(renderer, p), toScreen(renderer, q), item.color, kWidth);
  };
  for (const Item& item : _items) {
    if (item.screen != screen) continue;
    if (item.shape == Shape::Line) line(item, item.a, item.b);
    if (item.shape == Shape::Rect) {
      const glm::vec2 lo = item.a - item.b * 0.5f, hi = item.a + item.b * 0.5f;
      line(item, lo, {hi.x, lo.y});
      line(item, {hi.x, lo.y}, hi);
      line(item, hi, {lo.x, hi.y});
      line(item, {lo.x, hi.y}, lo);
    }
    if (item.shape == Shape::Circle) {
      constexpr int kSides = 24;
      const auto at = [&](int i) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kSides;
        return item.a + item.b.x * glm::vec2(std::cos(angle), std::sin(angle));
      };
      for (int i = 0; i < kSides; ++i) line(item, at(i), at(i + 1));
    }
  }
}

nlohmann::json DebugDraw::describe() const {
  static constexpr const char* kShapes[] = {"line", "circle", "rect", "text"};
  nlohmann::json out = nlohmann::json::array();
  for (const Item& item : _items) {
    nlohmann::json entry = {{"shape", kShapes[static_cast<int>(item.shape)]},
                            {"space", item.screen ? "screen" : "world"},
                            {"color", {tidy(item.color.r), tidy(item.color.g), tidy(item.color.b)}}};
    if (item.shape == Shape::Line) {
      entry["from"] = tidy(item.a);
      entry["to"] = tidy(item.b);
    } else {
      entry["at"] = tidy(item.a);
    }
    if (item.shape == Shape::Circle) entry["radius"] = tidy(item.b.x);
    if (item.shape == Shape::Rect) entry["size"] = tidy(item.b);
    if (item.shape == Shape::Text) entry["text"] = item.text;
    if (item.seconds > 0.0f) entry["seconds"] = tidy(item.seconds);
    out.push_back(std::move(entry));
  }
  return out;
}
