#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>

#include "Css.hpp"
#include "Dom.hpp"

enum class Display { Block, Inline, Flex, None };
enum class FlexDirection { Row, Column };
enum class Justify { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Align { Start, Center, End, Stretch };
enum class Position { Static, Relative, Absolute };
enum class TextAlign { Left, Center, Right };

// Fully resolved style for one element (or text node, which copies its
// parent's inherited fields). Box edges are [top, right, bottom, left].
struct ComputedStyle {
  Display display = Display::Block;
  FlexDirection flexDirection = FlexDirection::Row;
  Justify justifyContent = Justify::Start;
  Align alignItems = Align::Stretch;
  float gap = 0.0f;
  float flexGrow = 0.0f;
  bool flexWrap = false;

  Position position = Position::Static;
  Length top, right, bottom, left;
  Length width, height;
  Length minWidth, minHeight, maxWidth, maxHeight;
  std::array<Length, 4> margin{Length::px(0), Length::px(0), Length::px(0), Length::px(0)};
  std::array<float, 4> padding{0, 0, 0, 0};
  std::array<float, 4> borderWidth{0, 0, 0, 0};
  glm::vec4 borderColor{0, 0, 0, 0};
  glm::vec4 backgroundColor{0, 0, 0, 0};
  std::string backgroundImage;  // asset path or "atlas.json#region"
  float opacity = 1.0f;
  int zIndex = 0;

  // Inherited.
  glm::vec4 color{1, 1, 1, 1};
  float fontSize = 16.0f;
  std::string fontFamily;  // font asset path; empty = UI default font
  TextAlign textAlign = TextAlign::Left;
  float lineHeight = 1.25f;  // multiple of fontSize
  float letterSpacing = 0.0f;
  bool visible = true;
  bool crispText = false;  // `font-smooth: never`: 1-bit glyphs (pixel fonts)
  glm::vec2 textShadowOffset{0, 0};
  glm::vec4 textShadowColor{0, 0, 0, 0};
  bool uppercase = false;
};

// Computes the style of `node` given its parent's computed style. Cascade:
// tag defaults < stylesheet rules (specificity, then source order) < inline
// style attribute. `viewport` is the logical screen size for vw/vh.
ComputedStyle computeStyle(const UINode& node, const ComputedStyle* parent,
                           const Stylesheet& sheet, glm::vec2 viewport);

// Applies one declaration onto `style`. Unknown properties and unparsable
// values are ignored (forgiving, like browsers).
void applyDeclaration(ComputedStyle& style, const CssDeclaration& decl, glm::vec2 viewport);
