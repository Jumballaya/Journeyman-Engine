#include "Style.hpp"

#include <algorithm>
#include <cctype>

namespace {

std::string lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

float px(const std::string& v, glm::vec2 viewport, float fallback = 0.0f) {
  auto l = parseLength(v, viewport);
  return (l && l->unit == Length::Unit::Px) ? l->value : fallback;
}

// 1–4 value shorthand → [top, right, bottom, left].
template <typename T, typename Parse>
bool boxShorthand(const std::string& value, std::array<T, 4>& out, Parse parse) {
  auto parts = splitValue(value);
  if (parts.empty() || parts.size() > 4) return false;
  std::array<T, 4> v{};
  for (size_t i = 0; i < parts.size(); ++i) {
    auto parsed = parse(parts[i]);
    if (!parsed) return false;
    v[i] = *parsed;
  }
  switch (parts.size()) {
    case 1: out = {v[0], v[0], v[0], v[0]}; break;
    case 2: out = {v[0], v[1], v[0], v[1]}; break;
    case 3: out = {v[0], v[1], v[2], v[1]}; break;
    default: out = v; break;
  }
  return true;
}

void applyTagDefaults(ComputedStyle& s, const std::string& tag) {
  if (tag == "span" || tag == "b" || tag == "strong" || tag == "i" || tag == "em" || tag == "a" ||
      tag == "small" || tag == "label") {
    s.display = Display::Inline;
  }
  if (tag == "link" || tag == "meta" || tag == "br") {
    s.display = Display::None;
  }
  if (tag == "h1") {
    s.fontSize = 32;
    s.margin[2] = Length::px(16);
  } else if (tag == "h2") {
    s.fontSize = 24;
    s.margin[2] = Length::px(12);
  } else if (tag == "h3") {
    s.fontSize = 18;
    s.margin[2] = Length::px(8);
  } else if (tag == "p") {
    s.margin[2] = Length::px(8);
  } else if (tag == "small") {
    s.fontSize *= 0.75f;
  }
}

}  // namespace

void applyDeclaration(ComputedStyle& s, const CssDeclaration& d, glm::vec2 vp) {
  const std::string& p = d.property;
  const std::string v = lower(d.value);
  auto length = [&](Length& out) {
    if (auto l = parseLength(v, vp)) out = *l;
  };
  auto parseLen = [&](const std::string& x) { return parseLength(x, vp); };
  auto parsePx = [&](const std::string& x) -> std::optional<float> {
    auto l = parseLength(x, vp);
    if (!l || l->unit != Length::Unit::Px) return std::nullopt;
    return l->value;
  };

  if (p == "display") {
    if (v == "none") s.display = Display::None;
    else if (v == "flex") s.display = Display::Flex;
    else if (v == "inline" || v == "inline-block") s.display = Display::Inline;
    else s.display = Display::Block;
  } else if (p == "flex-direction") {
    s.flexDirection = v.starts_with("column") ? FlexDirection::Column : FlexDirection::Row;
  } else if (p == "flex-wrap") {
    s.flexWrap = v == "wrap";
  } else if (p == "justify-content") {
    if (v == "center") s.justifyContent = Justify::Center;
    else if (v == "flex-end" || v == "end" || v == "right") s.justifyContent = Justify::End;
    else if (v == "space-between") s.justifyContent = Justify::SpaceBetween;
    else if (v == "space-around") s.justifyContent = Justify::SpaceAround;
    else if (v == "space-evenly") s.justifyContent = Justify::SpaceEvenly;
    else s.justifyContent = Justify::Start;
  } else if (p == "align-items") {
    if (v == "center") s.alignItems = Align::Center;
    else if (v == "flex-end" || v == "end") s.alignItems = Align::End;
    else if (v == "flex-start" || v == "start" || v == "baseline") s.alignItems = Align::Start;
    else s.alignItems = Align::Stretch;
  } else if (p == "gap") {
    s.gap = px(v, vp, s.gap);
  } else if (p == "flex-grow") {
    s.flexGrow = std::strtof(v.c_str(), nullptr);
  } else if (p == "flex") {
    auto parts = splitValue(v);
    if (!parts.empty()) s.flexGrow = parts[0] == "none" ? 0.0f : std::strtof(parts[0].c_str(), nullptr);
  } else if (p == "position") {
    s.position = v == "absolute" || v == "fixed" ? Position::Absolute
               : v == "relative"                 ? Position::Relative
                                                 : Position::Static;
  } else if (p == "top") length(s.top);
  else if (p == "right") length(s.right);
  else if (p == "bottom") length(s.bottom);
  else if (p == "left") length(s.left);
  else if (p == "inset") {
    std::array<Length, 4> box;
    if (boxShorthand(v, box, parseLen)) {
      s.top = box[0]; s.right = box[1]; s.bottom = box[2]; s.left = box[3];
    }
  }
  else if (p == "width") length(s.width);
  else if (p == "height") length(s.height);
  else if (p == "min-width") length(s.minWidth);
  else if (p == "min-height") length(s.minHeight);
  else if (p == "max-width") length(s.maxWidth);
  else if (p == "max-height") length(s.maxHeight);
  else if (p == "margin") boxShorthand(v, s.margin, parseLen);
  else if (p == "margin-top") length(s.margin[0]);
  else if (p == "margin-right") length(s.margin[1]);
  else if (p == "margin-bottom") length(s.margin[2]);
  else if (p == "margin-left") length(s.margin[3]);
  else if (p == "padding") boxShorthand(v, s.padding, parsePx);
  else if (p == "padding-top") s.padding[0] = px(v, vp, s.padding[0]);
  else if (p == "padding-right") s.padding[1] = px(v, vp, s.padding[1]);
  else if (p == "padding-bottom") s.padding[2] = px(v, vp, s.padding[2]);
  else if (p == "padding-left") s.padding[3] = px(v, vp, s.padding[3]);
  else if (p == "border" || p == "border-top" || p == "border-right" || p == "border-bottom" || p == "border-left") {
    // "<width> [style] <color>" in any order.
    float width = 0.0f;
    glm::vec4 color = s.borderColor;
    bool none = false;
    for (const auto& part : splitValue(v)) {
      if (part == "none") none = true;
      else if (auto w = parsePx(part)) width = *w;
      else if (auto c = parseColor(part)) color = *c;
    }
    if (none) width = 0.0f;
    const int side = p == "border-top" ? 0 : p == "border-right" ? 1 : p == "border-bottom" ? 2 : p == "border-left" ? 3 : -1;
    if (side < 0) s.borderWidth = {width, width, width, width};
    else s.borderWidth[side] = width;
    s.borderColor = color;
  } else if (p == "border-width") boxShorthand(v, s.borderWidth, parsePx);
  else if (p == "border-color") {
    if (auto c = parseColor(v)) s.borderColor = *c;
  } else if (p == "background-color" || p == "background") {
    if (auto c = parseColor(v)) {
      s.backgroundColor = *c;
    } else if (v.starts_with("url(")) {
      std::string url = d.value.substr(4, d.value.find(')') - 4);
      url.erase(std::remove(url.begin(), url.end(), '"'), url.end());
      url.erase(std::remove(url.begin(), url.end(), '\''), url.end());
      s.backgroundImage = url;
    }
  } else if (p == "background-image") {
    if (v == "none") {
      s.backgroundImage.clear();
    } else if (v.starts_with("url(")) {
      std::string url = d.value.substr(4, d.value.find(')') - 4);
      url.erase(std::remove(url.begin(), url.end(), '"'), url.end());
      url.erase(std::remove(url.begin(), url.end(), '\''), url.end());
      s.backgroundImage = url;
    }
  } else if (p == "opacity") {
    s.opacity = std::clamp(std::strtof(v.c_str(), nullptr), 0.0f, 1.0f);
  } else if (p == "z-index") {
    s.zIndex = static_cast<int>(std::strtol(v.c_str(), nullptr, 10));
  } else if (p == "color") {
    if (auto c = parseColor(v)) s.color = *c;
  } else if (p == "font-size") {
    s.fontSize = px(v, vp, s.fontSize);
  } else if (p == "font-family") {
    std::string f = d.value;
    f.erase(std::remove(f.begin(), f.end(), '"'), f.end());
    f.erase(std::remove(f.begin(), f.end(), '\''), f.end());
    s.fontFamily = f;
  } else if (p == "text-align") {
    s.textAlign = v == "center" ? TextAlign::Center : v == "right" ? TextAlign::Right : TextAlign::Left;
  } else if (p == "line-height") {
    auto l = parseLength(v, vp);
    if (l && l->unit == Length::Unit::Px && v.find("px") != std::string::npos && s.fontSize > 0) {
      s.lineHeight = l->value / s.fontSize;
    } else if (l && l->unit == Length::Unit::Percent) {
      s.lineHeight = l->value / 100.0f;
    } else if (l) {
      s.lineHeight = l->value;
    }
  } else if (p == "letter-spacing") {
    s.letterSpacing = px(v, vp, s.letterSpacing);
  } else if (p == "visibility") {
    s.visible = v != "hidden";
  } else if (p == "font-smooth" || p == "-webkit-font-smoothing") {
    s.crispText = v == "never" || v == "none";
  } else if (p == "text-shadow") {
    if (v == "none") {
      s.textShadowColor = glm::vec4(0);
    } else {
      std::vector<float> nums;
      glm::vec4 color{0, 0, 0, 1};
      for (const auto& part : splitValue(v)) {
        if (auto n = parsePx(part)) nums.push_back(*n);
        else if (auto c = parseColor(part)) color = *c;
      }
      if (nums.size() >= 2) {
        s.textShadowOffset = {nums[0], nums[1]};
        s.textShadowColor = color;
      }
    }
  } else if (p == "text-transform") {
    s.uppercase = v == "uppercase";
  }
}

ComputedStyle computeStyle(const UINode& node, const ComputedStyle* parent,
                           const Stylesheet& sheet, glm::vec2 viewport) {
  ComputedStyle s;
  if (parent) {
    s.color = parent->color;
    s.fontSize = parent->fontSize;
    s.fontFamily = parent->fontFamily;
    s.textAlign = parent->textAlign;
    s.lineHeight = parent->lineHeight;
    s.letterSpacing = parent->letterSpacing;
    s.visible = parent->visible;
    s.crispText = parent->crispText;
    s.textShadowOffset = parent->textShadowOffset;
    s.textShadowColor = parent->textShadowColor;
    s.uppercase = parent->uppercase;
  }
  if (node.isText()) {
    s.display = Display::Inline;
    return s;
  }
  applyTagDefaults(s, node.tag);

  std::vector<const CssRule*> matched;
  for (const auto& rule : sheet.rules) {
    if (rule.selector.matches(node)) matched.push_back(&rule);
  }
  std::stable_sort(matched.begin(), matched.end(), [](const CssRule* a, const CssRule* b) {
    if (a->selector.specificity != b->selector.specificity) return a->selector.specificity < b->selector.specificity;
    return a->order < b->order;
  });
  // CSS cascade order: normal rules < inline style < !important rules <
  // !important inline. (So `.hidden { display: none !important }` beats an
  // id selector, as in browsers.)
  const auto inlineDecls = parseDeclarations(node.inlineStyle);
  for (bool important : {false, true}) {
    for (const CssRule* rule : matched) {
      for (const auto& d : rule->declarations) {
        if (d.important == important) applyDeclaration(s, d, viewport);
      }
    }
    for (const auto& d : inlineDecls) {
      if (d.important == important) applyDeclaration(s, d, viewport);
    }
  }
  return s;
}
