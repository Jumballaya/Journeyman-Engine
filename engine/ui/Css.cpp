#include "Css.hpp"

#include <cstdlib>
#include <unordered_map>

#include "Text.hpp"

namespace {

std::string stripComments(std::string_view css) {
  std::string out;
  for (size_t i = 0; i < css.size(); ++i) {
    if (css.substr(i, 2) == "/*") {
      const size_t end = css.find("*/", i + 2);
      if (end == std::string_view::npos) break;
      i = end + 1;
      continue;
    }
    out += css[i];
  }
  return out;
}

bool isNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

// Parses one compound ("div.a#b"). Returns nullopt for unsupported syntax
// (pseudo-classes, attribute selectors) so the whole selector is dropped.
std::optional<CssCompound> parseCompound(std::string_view s, std::array<int, 3>& spec) {
  CssCompound c;
  size_t i = 0;
  auto readName = [&]() {
    const size_t start = i;
    while (i < s.size() && isNameChar(s[i])) ++i;
    return std::string(s.substr(start, i - start));
  };
  if (i < s.size() && s[i] == '*') {
    ++i;
  } else if (i < s.size() && isNameChar(s[i])) {
    c.tag = lower(readName());
    ++spec[2];
  }
  while (i < s.size()) {
    const char kind = s[i++];
    if (kind == '.') {
      c.classes.push_back(readName());
      ++spec[1];
    } else if (kind == '#') {
      c.id = readName();
      ++spec[0];
    } else {
      return std::nullopt;
    }
  }
  return c;
}

std::optional<CssSelector> parseSelector(std::string_view text) {
  CssSelector sel;
  std::string token;
  char pending = 0;  // combinator waiting for the next compound
  auto flush = [&]() -> bool {
    if (token.empty()) return true;
    auto compound = parseCompound(token, sel.specificity);
    token.clear();
    if (!compound) return false;
    if (!sel.parts.empty()) sel.combinators.push_back(pending ? pending : ' ');
    sel.parts.push_back(std::move(*compound));
    pending = 0;
    return true;
  };
  for (char c : text) {
    if (isSpace(c)) {
      if (!flush()) return std::nullopt;
    } else if (c == '>') {
      if (!flush()) return std::nullopt;
      pending = '>';
    } else {
      token += c;
    }
  }
  if (!flush() || sel.parts.empty()) return std::nullopt;
  return sel;
}

// Right to left, backtracking over ancestors for descendant combinators.
bool matchesFrom(const CssSelector& sel, const UINode* n, size_t part) {
  if (!n || !sel.parts[part].matches(*n)) return false;
  if (part == 0) return true;
  if (sel.combinators[part - 1] == '>') return matchesFrom(sel, n->parent, part - 1);
  for (const UINode* a = n->parent; a; a = a->parent) {
    if (matchesFrom(sel, a, part - 1)) return true;
  }
  return false;
}

// Non-empty pieces of `s` between separators outside parentheses.
template <typename IsSeparator>
std::vector<std::string_view> splitTopLevel(std::string_view s, IsSeparator isSeparator) {
  std::vector<std::string_view> out;
  int parens = 0;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    const bool end = i == s.size();
    if (!end && s[i] == '(') ++parens;
    if (!end && s[i] == ')') --parens;
    if (!end && (parens != 0 || !isSeparator(s[i]))) continue;
    if (i > start) out.push_back(s.substr(start, i - start));
    start = i + 1;
  }
  return out;
}

}  // namespace

bool CssCompound::matches(const UINode& node) const {
  if (node.isText()) return false;
  if (!tag.empty() && node.tag != tag) return false;
  if (!id.empty() && node.id != id) return false;
  for (const auto& c : classes) {
    if (!node.hasClass(c)) return false;
  }
  return true;
}

bool CssSelector::matches(const UINode& node) const { return matchesFrom(*this, &node, parts.size() - 1); }

std::vector<CssDeclaration> parseDeclarations(std::string_view block) {
  std::vector<CssDeclaration> out;
  for (std::string_view piece : splitTopLevel(block, [](char c) { return c == ';'; })) {
    const size_t colon = piece.find(':');
    if (colon == std::string_view::npos) continue;
    std::string property = lower(trim(piece.substr(0, colon)));
    std::string_view value = trim(piece.substr(colon + 1));
    const bool important = value.size() >= 10 && lower(value.substr(value.size() - 10)) == "!important";
    if (important) value = trim(value.substr(0, value.size() - 10));
    if (!property.empty()) out.push_back({std::move(property), std::string(value), important});
  }
  return out;
}

void Stylesheet::append(std::string_view rawCss) {
  const std::string css = stripComments(rawCss);
  size_t i = 0;
  while (i < css.size()) {
    const size_t open = css.find('{', i);
    if (open == std::string::npos) break;
    const std::string_view prelude = trim(std::string_view(css).substr(i, open - i));
    // Matching close brace (one level of nesting for @media etc., skipped).
    size_t close = open + 1;
    for (int depth = 1; close < css.size() && depth > 0; ++close) {
      if (css[close] == '{') ++depth;
      if (css[close] == '}') --depth;
    }
    const std::string_view body = std::string_view(css).substr(open + 1, close - open - 2);
    i = close;
    if (prelude.empty() || prelude[0] == '@') continue;

    const auto declarations = parseDeclarations(body);
    for (std::string_view selector : splitTopLevel(prelude, [](char c) { return c == ','; })) {
      if (auto sel = parseSelector(trim(selector))) rules.push_back(CssRule{std::move(*sel), declarations, rules.size()});
    }
  }
}

std::vector<std::string> splitValue(std::string_view v) {
  std::vector<std::string> parts;
  for (std::string_view piece : splitTopLevel(v, isSpace)) parts.emplace_back(piece);
  return parts;
}

std::optional<Length> parseLength(std::string_view raw, glm::vec2 viewport) {
  const std::string v = lower(trim(raw));
  if (v.empty()) return std::nullopt;
  if (v == "auto") return Length{};
  char* end = nullptr;
  const float n = std::strtof(v.c_str(), &end);
  if (end == v.c_str()) return std::nullopt;
  const std::string unit(end);
  if (unit.empty() || unit == "px") return Length::px(n);
  if (unit == "%") return Length{Length::Unit::Percent, n};
  if (unit == "vw") return Length::px(n * 0.01f * viewport.x);
  if (unit == "vh") return Length::px(n * 0.01f * viewport.y);
  if (unit == "em" || unit == "rem") return Length::px(n * 16.0f);
  return std::nullopt;
}

std::optional<glm::vec4> parseColor(std::string_view raw) {
  const std::string v = lower(trim(raw));
  if (v.empty()) return std::nullopt;
  if (v[0] == '#') {
    std::string hex = v.substr(1);
    if (hex.size() == 3 || hex.size() == 4) {
      std::string expanded;
      for (char c : hex) expanded += std::string(2, c);
      hex = expanded;
    }
    if (hex.size() != 6 && hex.size() != 8) return std::nullopt;
    if (!std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); })) return std::nullopt;
    auto byte = [&](size_t at) { return std::strtoul(hex.substr(at, 2).c_str(), nullptr, 16) / 255.0f; };
    return glm::vec4(byte(0), byte(2), byte(4), hex.size() == 8 ? byte(6) : 1.0f);
  }
  if (v.starts_with("rgb")) {
    const size_t open = v.find('(');
    const size_t close = v.find(')');
    if (open == std::string::npos || close == std::string::npos) return std::nullopt;
    std::string inner = v.substr(open + 1, close - open - 1);
    std::replace(inner.begin(), inner.end(), ',', ' ');
    std::replace(inner.begin(), inner.end(), '/', ' ');
    auto parts = splitValue(inner);
    if (parts.size() < 3) return std::nullopt;
    auto channel = [](const std::string& p) {
      const float n = std::strtof(p.c_str(), nullptr);
      return p.back() == '%' ? n / 100.0f : n / 255.0f;
    };
    float alpha = 1.0f;
    if (parts.size() >= 4) {
      alpha = std::strtof(parts[3].c_str(), nullptr);
      if (parts[3].back() == '%') alpha /= 100.0f;
    }
    return glm::vec4(channel(parts[0]), channel(parts[1]), channel(parts[2]), alpha);
  }
  static const std::unordered_map<std::string, glm::vec4> kNamed = {
      {"transparent", {0, 0, 0, 0}},     {"black", {0, 0, 0, 1}},
      {"white", {1, 1, 1, 1}},           {"red", {1, 0, 0, 1}},
      {"green", {0, 0.5f, 0, 1}},        {"lime", {0, 1, 0, 1}},
      {"blue", {0, 0, 1, 1}},            {"yellow", {1, 1, 0, 1}},
      {"orange", {1, 0.647f, 0, 1}},     {"gold", {1, 0.843f, 0, 1}},
      {"gray", {0.5f, 0.5f, 0.5f, 1}},   {"grey", {0.5f, 0.5f, 0.5f, 1}},
      {"silver", {0.75f, 0.75f, 0.75f, 1}}, {"cyan", {0, 1, 1, 1}},
      {"magenta", {1, 0, 1, 1}},         {"navy", {0, 0, 0.5f, 1}},
      {"purple", {0.5f, 0, 0.5f, 1}},    {"pink", {1, 0.753f, 0.796f, 1}},
  };
  auto it = kNamed.find(v);
  if (it != kNamed.end()) return it->second;
  return std::nullopt;
}
