#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Dom.hpp"

// CSS subset: type, .class, #id, * and compound selectors with descendant and
// child combinators. Pseudo-classes and @-rules are skipped.
struct CssDeclaration {
  std::string property;  // lowercase
  std::string value;     // trimmed, original case, without "!important"
  bool important = false;
};

struct CssCompound {
  std::string tag;  // empty = any
  std::string id;
  std::vector<std::string> classes;
  bool matches(const UINode& node) const;
};

struct CssSelector {
  // Rightmost compound last. combinators[i] joins parts[i] and parts[i+1]:
  // ' ' = descendant, '>' = child.
  std::vector<CssCompound> parts;
  std::vector<char> combinators;
  std::array<int, 3> specificity{0, 0, 0};  // (ids, classes, tags)
  bool matches(const UINode& node) const;
};

struct CssRule {
  CssSelector selector;
  std::vector<CssDeclaration> declarations;
  size_t order = 0;  // source order, breaks specificity ties
};

struct Stylesheet {
  std::vector<CssRule> rules;
  void append(std::string_view css);
};

// Parses "a: b; c: d" (inline style attributes, setStyle values).
std::vector<CssDeclaration> parseDeclarations(std::string_view block);

// ---- Value parsing ---------------------------------------------------------

// A CSS length. Percent resolves against the relevant containing size.
struct Length {
  enum class Unit { Auto, Px, Percent };
  Unit unit = Unit::Auto;
  float value = 0.0f;

  static Length px(float v) { return {Unit::Px, v}; }
  bool isAuto() const { return unit == Unit::Auto; }
  float resolve(float relativeTo, float fallback = 0.0f) const {
    switch (unit) {
      case Unit::Px: return value;
      case Unit::Percent: return value * 0.01f * relativeTo;
      default: return fallback;
    }
  }
};

// px, %, unitless (= px), vw/vh (against `viewport`), em (times `em`, the
// font size it's relative to), rem (times 16, the root size), auto.
std::optional<Length> parseLength(std::string_view v, glm::vec2 viewport, float em = 16.0f);
// #rgb, #rgba, #rrggbb, #rrggbbaa, rgb(), rgba(), named colors, transparent.
std::optional<glm::vec4> parseColor(std::string_view v);
// Splits a value on whitespace outside parentheses ("1px solid rgb(0, 0, 0)").
std::vector<std::string> splitValue(std::string_view v);
