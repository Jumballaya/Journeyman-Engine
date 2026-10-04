#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Css.hpp"
#include "Dom.hpp"
#include "Style.hpp"

// Font/image metrics the layout needs, provided by the UI module (real
// glyph metrics) or by tests (fixed-width fakes).
class LayoutMetrics {
 public:
  virtual ~LayoutMetrics() = default;
  virtual float textWidth(const ComputedStyle& style, std::string_view text) = 0;
  virtual glm::vec2 imageSize(const std::string& src) = 0;
};

// A positioned run of text on one line, in logical pixels.
struct TextPiece {
  std::string text;
  const ComputedStyle* style;  // points into the owning LayoutBox tree
  float x = 0.0f;
  float lineTop = 0.0f;
  float lineHeight = 0.0f;
  float width = 0.0f;
};

// The laid-out form of an element: its border box plus children and any
// text lines it owns. Built fresh by layoutDocument; pointers stay valid
// until the next layout.
struct LayoutBox {
  const UINode* node = nullptr;
  ComputedStyle style;
  glm::vec4 rect{0.0f};  // border box: x, y, w, h (absolute, logical px)
  std::vector<std::unique_ptr<LayoutBox>> children;
  std::vector<std::unique_ptr<ComputedStyle>> runStyles;  // styles of inline elements/text
  std::vector<TextPiece> text;
};

// Styles and lays out `root`'s subtree into a box of `viewport` size.
std::unique_ptr<LayoutBox> layoutDocument(const UINode& root, const Stylesheet& sheet,
                                          glm::vec2 viewport, LayoutMetrics& metrics);
