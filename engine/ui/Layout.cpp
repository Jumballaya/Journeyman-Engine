#include "Layout.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace {

constexpr int kTop = 0, kRight = 1, kBottom = 2, kLeft = 3;

float horizontalExtras(const ComputedStyle& s) {
  return s.padding[kLeft] + s.padding[kRight] + s.borderWidth[kLeft] + s.borderWidth[kRight];
}

float verticalExtras(const ComputedStyle& s) {
  return s.padding[kTop] + s.padding[kBottom] + s.borderWidth[kTop] + s.borderWidth[kBottom];
}

float clampSize(float v, const Length& minL, const Length& maxL, float relativeTo) {
  if (!maxL.isAuto()) v = std::min(v, maxL.resolve(relativeTo));
  if (!minL.isAuto()) v = std::max(v, minL.resolve(relativeTo));
  return std::max(v, 0.0f);
}

struct Run {
  std::string text;  // "\n" for <br>
  const ComputedStyle* style;
};

// Text without its leading and trailing spaces.
std::string_view trimmed(std::string_view text) {
  const size_t first = text.find_first_not_of(' ');
  if (first == std::string_view::npos) return {};
  return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

Run textRun(std::string text, const ComputedStyle& style) {
  if (style.uppercase) std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::toupper(c); });
  return {std::move(text), &style};
}

class Layouter {
 public:
  Layouter(const Stylesheet& sheet, glm::vec2 viewport, LayoutMetrics& metrics)
      : _sheet(sheet), _viewport(viewport), _metrics(metrics) {}

  // Boxes for `node` and its displayed element descendants; text is
  // collected as runs during layout. As in CSS, each text child of a flex
  // container becomes an anonymous flex item (a box whose node is the text;
  // whitespace-only text is dropped).
  std::unique_ptr<LayoutBox> build(const UINode& node, ComputedStyle style) {
    auto box = std::make_unique<LayoutBox>();
    box->node = &node;
    box->style = std::move(style);
    const bool flex = box->style.display == Display::Flex;
    for (const auto& child : node.children) {
      if (child->isText()) {
        if (!flex || trimmed(child->text).empty()) continue;
        auto item = std::make_unique<LayoutBox>();
        item->node = child.get();
        item->style = computeStyle(*child, &box->style, _sheet, _viewport);
        item->style.display = Display::Block;
        box->children.push_back(std::move(item));
        continue;
      }
      ComputedStyle childStyle = computeStyle(*child, &box->style, _sheet, _viewport);
      if (childStyle.display != Display::None) box->children.push_back(build(*child, std::move(childStyle)));
    }
    return box;
  }

  // Lays out `b` with its border-box top-left at (x, y) and border-box
  // width `width`. `containerH` resolves percentage heights (< 0 = unknown).
  void layout(LayoutBox& b, float x, float y, float width, float containerH, std::optional<float> forcedH = {}) {
    const ComputedStyle& s = b.style;
    b.rect = glm::vec4(x, y, width, 0.0f);
    b.text.clear();

    const float cx = x + s.borderWidth[kLeft] + s.padding[kLeft];
    const float cy = y + s.borderWidth[kTop] + s.padding[kTop];
    const float cw = std::max(0.0f, width - horizontalExtras(s));

    std::optional<float> definiteH = forcedH;
    if (!definiteH && !s.height.isAuto() && (s.height.unit == Length::Unit::Px || containerH >= 0)) {
      definiteH = s.height.resolve(containerH);
    }
    std::optional<float> contentDefiniteH;
    if (definiteH) contentDefiniteH = std::max(0.0f, *definiteH - verticalExtras(s));

    float contentH = 0.0f;
    if (b.node->tag == "img") {
      const glm::vec2 natural = imageSize(b);
      contentH = natural.x > 0 ? cw * natural.y / natural.x : natural.y;
    } else if (s.display == Display::Flex) {
      contentH = layoutFlex(b, cx, cy, cw, contentDefiniteH);
    } else {
      contentH = layoutBlock(b, cx, cy, cw, contentDefiniteH ? *contentDefiniteH : -1.0f);
    }

    float h = definiteH ? *definiteH : contentH + verticalExtras(s);
    b.rect.w = clampSize(h, s.minHeight, s.maxHeight, containerH);
    layoutAbsolute(b);
  }

  // Width the box would take with unlimited space (max-content), including
  // padding and border.
  float intrinsicWidth(LayoutBox& b) {
    const ComputedStyle& s = b.style;
    if (s.width.unit == Length::Unit::Px) return s.width.value;
    float content = 0.0f;
    if (b.node->tag == "img") {
      content = imageSize(b).x;
    } else if (hasOnlyInlineContent(b)) {
      std::vector<Run> runs;
      collectRuns(b, runs);
      float line = 0.0f;
      for (const auto& r : runs) {
        line = r.text == "\n" ? 0.0f : line + _metrics.textWidth(*r.style, r.text);
        content = std::max(content, line);
      }
    } else if (s.display == Display::Flex && s.flexDirection == FlexDirection::Row) {
      const auto items = inFlow(b);
      for (LayoutBox* c : items) content += intrinsicWidth(*c) + marginsX(c->style);
      if (items.size() > 1) content += s.gap * static_cast<float>(items.size() - 1);
    } else {
      for (LayoutBox* c : inFlow(b)) content = std::max(content, intrinsicWidth(*c) + marginsX(c->style));
      // Loose text between block children gets lines of its own.
      for (const auto& child : b.node->children) {
        if (!child->isText() || s.display == Display::Flex) continue;  // flex text has boxes
        const Run run = textRun(child->text, b.style);
        content = std::max(content, _metrics.textWidth(*run.style, run.text));
      }
    }
    return content + horizontalExtras(s);
  }

 private:
  const Stylesheet& _sheet;
  glm::vec2 _viewport;
  LayoutMetrics& _metrics;

  glm::vec2 imageSize(const LayoutBox& b) {
    auto src = b.node->attributes.find("src");
    return _metrics.imageSize(src == b.node->attributes.end() ? "" : src->second);
  }

  float marginPx(const ComputedStyle& s, int side) const {
    return s.margin[side].isAuto() ? 0.0f : s.margin[side].resolve(_viewport.x);
  }
  float marginsX(const ComputedStyle& s) const { return marginPx(s, kLeft) + marginPx(s, kRight); }
  float marginsY(const ComputedStyle& s) const { return marginPx(s, kTop) + marginPx(s, kBottom); }

  // In-flow element children. Flex containers blockify inline children
  // (a <span> in a flex row is a flex item), as in CSS.
  std::vector<LayoutBox*> inFlow(LayoutBox& b) {
    const bool flex = b.style.display == Display::Flex;
    std::vector<LayoutBox*> out;
    for (auto& c : b.children) {
      if (c->style.position == Position::Absolute) continue;
      if (!flex && c->style.display == Display::Inline) continue;
      out.push_back(c.get());
    }
    return out;
  }

  // True when `b` holds text and inline elements only (<br> counts as inline).
  bool hasOnlyInlineContent(const LayoutBox& b) const {
    if (b.node->isText()) return true;  // an anonymous flex item
    for (const auto& c : b.children) {
      if (c->node->tag == "br") continue;
      if (c->style.display != Display::Inline || c->style.position == Position::Absolute) return false;
    }
    return !b.children.empty() || std::any_of(b.node->children.begin(), b.node->children.end(), [](const auto& c) {
             return c->isText() || c->tag == "br";
           });
  }

  // Flattens the text under `box` (text nodes and inline elements, whose
  // boxes own the styles) into styled runs.
  void collectRuns(const LayoutBox& box, std::vector<Run>& out) {
    if (box.node->isText()) {  // an anonymous flex item: its spaces sit between items
      out.push_back(textRun(std::string(trimmed(box.node->text)), box.style));
      return;
    }
    size_t next = 0;  // box.children follow box.node->children in order
    for (const auto& child : box.node->children) {
      const LayoutBox* childBox = nullptr;
      if (next < box.children.size() && box.children[next]->node == child.get()) childBox = box.children[next++].get();
      if (child->isText()) out.push_back(textRun(child->text, box.style));
      else if (child->tag == "br") out.push_back({"\n", &box.style});
      else if (childBox) collectRuns(*childBox, out);
    }
  }

  // Greedy word wrap of `runs` into lines starting at (cx, y) within cw.
  // Returns the total height.
  float layoutLines(LayoutBox& owner, const std::vector<Run>& runs, float cx, float y, float cw, TextAlign align) {
    struct Word {
      std::string text;
      const ComputedStyle* style;
      bool spaceBefore;
      bool lineBreak = false;  // <br>: ends the current line
    };
    std::vector<Word> words;
    for (const auto& r : runs) {
      if (r.text == "\n") {
        words.push_back({"", r.style, false, true});
        continue;
      }
      size_t i = 0;
      bool space = !r.text.empty() && r.text[0] == ' ';
      while (i < r.text.size()) {
        while (i < r.text.size() && r.text[i] == ' ') {
          space = true;
          ++i;
        }
        const size_t start = i;
        while (i < r.text.size() && r.text[i] != ' ') ++i;
        if (i > start) words.push_back({r.text.substr(start, i - start), r.style, space});
        space = false;
      }
      if (!r.text.empty() && r.text.back() == ' ' && !words.empty()) {
        // Trailing space separates this run from the next one.
        words.push_back({"", r.style, true});
      }
    }

    float lineY = y;
    size_t w = 0;
    while (w < words.size()) {
      // Fill one line.
      std::vector<TextPiece> line;
      float lineW = 0.0f;
      float lineH = 0.0f;
      bool first = true;
      while (w < words.size()) {
        const Word& word = words[w];
        if (word.lineBreak) {
          ++w;
          lineH = std::max(lineH, word.style->fontSize * word.style->lineHeight);
          break;
        }
        const float spaceW = (!first && word.spaceBefore) ? _metrics.textWidth(*word.style, " ") : 0.0f;
        const float wordW = _metrics.textWidth(*word.style, word.text);
        if (!first && lineW + spaceW + wordW > cw + 0.01f) break;
        // Merge with the previous piece when the style matches.
        const std::string text = (spaceW > 0 ? " " : "") + word.text;
        if (!line.empty() && line.back().style == word.style) {
          line.back().text += text;
          line.back().width += spaceW + wordW;
        } else {
          line.push_back(TextPiece{text, word.style, lineW, 0.0f, 0.0f, spaceW + wordW});
        }
        lineW += spaceW + wordW;
        lineH = std::max(lineH, word.style->fontSize * word.style->lineHeight);
        first = false;
        ++w;
      }
      const float offset = align == TextAlign::Center ? (cw - lineW) * 0.5f
                         : align == TextAlign::Right  ? (cw - lineW)
                                                      : 0.0f;
      for (auto& piece : line) {
        piece.x += cx + offset;
        piece.lineTop = lineY;
        piece.lineHeight = lineH;
        if (!piece.text.empty()) owner.text.push_back(std::move(piece));
      }
      lineY += lineH;
    }
    return lineY - y;
  }

  float layoutBlock(LayoutBox& b, float cx, float cy, float cw, float ch) {
    if (hasOnlyInlineContent(b)) {
      std::vector<Run> runs;
      collectRuns(b, runs);
      return layoutLines(b, runs, cx, cy, cw, b.style.textAlign);
    }

    // Children in document order: element boxes stack; loose text nodes get
    // their own line boxes between them.
    float cursor = cy;
    size_t boxIndex = 0;
    for (const auto& child : b.node->children) {
      if (child->isText()) {
        const std::vector<Run> runs{textRun(child->text, b.style)};
        cursor += layoutLines(b, runs, cx, cursor, cw, b.style.textAlign);
        continue;
      }
      if (boxIndex >= b.children.size() || b.children[boxIndex]->node != child.get()) continue;
      LayoutBox& c = *b.children[boxIndex++];
      if (c.style.position == Position::Absolute) continue;

      const ComputedStyle& cs = c.style;
      const float ml = marginPx(cs, kLeft), mr = marginPx(cs, kRight);
      float w;
      if (!cs.width.isAuto()) {
        w = cs.width.resolve(cw);
      } else if (cs.display == Display::Inline) {
        w = std::min(intrinsicWidth(c), cw - ml - mr);
      } else {
        w = cw - ml - mr;
      }
      w = clampSize(w, cs.minWidth, cs.maxWidth, cw);

      float x = cx + ml;
      const bool autoL = cs.margin[kLeft].isAuto(), autoR = cs.margin[kRight].isAuto();
      if (autoL && autoR) x = cx + (cw - w) * 0.5f;
      else if (autoL) x = cx + cw - w - mr;

      cursor += marginPx(cs, kTop);
      layout(c, x, cursor, w, ch);
      applyRelative(c);
      cursor += c.rect.w + marginPx(cs, kBottom);
    }
    return cursor - cy;
  }

  // Returns the content height.
  float layoutFlex(LayoutBox& b, float cx, float cy, float cw, std::optional<float> ch) {
    const ComputedStyle& s = b.style;
    const bool row = s.flexDirection == FlexDirection::Row;
    auto items = inFlow(b);
    if (items.empty()) return 0.0f;
    const float containerH = ch.value_or(-1.0f);

    // 1. Base sizes along the main axis (+ cross sizes for columns).
    std::vector<float> mainSize(items.size()), crossSize(items.size());
    float used = s.gap * static_cast<float>(items.size() - 1);
    float growTotal = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) {
      LayoutBox& it = *items[i];
      const ComputedStyle& is = it.style;
      const float mMain = row ? marginsX(is) : marginsY(is);
      if (row) {
        mainSize[i] = !is.width.isAuto() ? is.width.resolve(cw) : std::min(intrinsicWidth(it), cw);
        mainSize[i] = clampSize(mainSize[i], is.minWidth, is.maxWidth, cw);
      } else {
        const float room = cw - marginsX(is);
        const float w = !is.width.isAuto()               ? is.width.resolve(cw)
                      : s.alignItems == Align::Stretch ? room
                                                       : std::min(intrinsicWidth(it), room);
        crossSize[i] = clampSize(w, is.minWidth, is.maxWidth, cw);
        layout(it, 0, 0, crossSize[i], containerH);  // measure height
        mainSize[i] = it.rect.w;
      }
      used += mainSize[i] + mMain;
      growTotal += is.flexGrow;
    }

    // 2. Distribute free space.
    const std::optional<float> containerMain = row ? std::optional<float>(cw) : ch;
    float free = containerMain ? *containerMain - used : 0.0f;
    if (free > 0 && growTotal > 0) {
      for (size_t i = 0; i < items.size(); ++i) mainSize[i] += free * (items[i]->style.flexGrow / growTotal);
      free = 0;
    }
    free = std::max(free, 0.0f);

    // 3. Row items: lay out at final width to learn their heights.
    float crossExtent = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) {
      LayoutBox& it = *items[i];
      if (row) {
        layout(it, 0, 0, mainSize[i], containerH);
        crossSize[i] = it.rect.w;
      }
      crossExtent = std::max(crossExtent, crossSize[i] + (row ? marginsY(it.style) : marginsX(it.style)));
    }
    const float crossAvail = row ? (ch ? *ch : crossExtent) : cw;

    // 4. Justify along the main axis.
    const float n = static_cast<float>(items.size());
    float lead = 0.0f, between = s.gap;
    switch (s.justifyContent) {
      case Justify::Center: lead = free * 0.5f; break;
      case Justify::End: lead = free; break;
      case Justify::SpaceBetween: between += n > 1 ? free / (n - 1) : 0.0f; break;
      case Justify::SpaceAround: lead = free / n * 0.5f; between += free / n; break;
      case Justify::SpaceEvenly: lead = free / (n + 1); between += free / (n + 1); break;
      default: break;
    }

    // 5. Place.
    float cursor = (row ? cx : cy) + lead;
    for (size_t i = 0; i < items.size(); ++i) {
      LayoutBox& it = *items[i];
      const ComputedStyle& is = it.style;
      const float m0 = row ? marginPx(is, kLeft) : marginPx(is, kTop);
      const float m1 = row ? marginPx(is, kRight) : marginPx(is, kBottom);
      const float c0 = row ? marginPx(is, kTop) : marginPx(is, kLeft);
      const float c1 = row ? marginPx(is, kBottom) : marginPx(is, kRight);
      const bool autoCross = row ? is.height.isAuto() : is.width.isAuto();

      float cross = crossSize[i];
      if (s.alignItems == Align::Stretch && autoCross) cross = crossAvail - c0 - c1;
      float crossPos = c0;
      if (s.alignItems == Align::Center) crossPos = (crossAvail - cross - c0 - c1) * 0.5f + c0;
      else if (s.alignItems == Align::End) crossPos = crossAvail - cross - c1;

      cursor += m0;
      if (row) {
        layout(it, cursor, cy + crossPos, mainSize[i], containerH,
               s.alignItems == Align::Stretch && autoCross ? std::optional<float>(cross) : std::nullopt);
      } else {
        layout(it, cx + crossPos, cursor, cross, containerH, mainSize[i]);
      }
      applyRelative(it);
      cursor += mainSize[i] + m1 + between;
    }

    if (row) return ch ? *ch : crossExtent;
    return ch ? *ch : (cursor - between - cy);
  }

  void applyRelative(LayoutBox& c) {
    if (c.style.position != Position::Relative) return;
    float dx = 0.0f, dy = 0.0f;
    if (!c.style.left.isAuto()) dx = c.style.left.resolve(_viewport.x);
    else if (!c.style.right.isAuto()) dx = -c.style.right.resolve(_viewport.x);
    if (!c.style.top.isAuto()) dy = c.style.top.resolve(_viewport.y);
    else if (!c.style.bottom.isAuto()) dy = -c.style.bottom.resolve(_viewport.y);
    translate(c, dx, dy);
  }

  static void translate(LayoutBox& b, float dx, float dy) {
    b.rect.x += dx;
    b.rect.y += dy;
    for (auto& t : b.text) {
      t.x += dx;
      t.lineTop += dy;
    }
    for (auto& c : b.children) translate(*c, dx, dy);
  }

  // Absolute children are placed against this box's padding box.
  void layoutAbsolute(LayoutBox& b) {
    const ComputedStyle& s = b.style;
    const float px0 = b.rect.x + s.borderWidth[kLeft];
    const float py0 = b.rect.y + s.borderWidth[kTop];
    const float pw = b.rect.z - s.borderWidth[kLeft] - s.borderWidth[kRight];
    const float ph = b.rect.w - s.borderWidth[kTop] - s.borderWidth[kBottom];
    for (auto& cp : b.children) {
      LayoutBox& c = *cp;
      const ComputedStyle& cs = c.style;
      if (cs.position != Position::Absolute) continue;
      const bool hasL = !cs.left.isAuto(), hasR = !cs.right.isAuto();
      const bool hasT = !cs.top.isAuto(), hasB = !cs.bottom.isAuto();
      float w = !cs.width.isAuto() ? cs.width.resolve(pw)
              : (hasL && hasR) ? pw - cs.left.resolve(pw) - cs.right.resolve(pw)
                               : std::min(intrinsicWidth(c), pw);
      w = clampSize(w, cs.minWidth, cs.maxWidth, pw);
      std::optional<float> h;
      if (!cs.height.isAuto()) h = cs.height.resolve(ph);
      else if (hasT && hasB) h = ph - cs.top.resolve(ph) - cs.bottom.resolve(ph);

      float x = hasL ? px0 + cs.left.resolve(pw) + marginPx(cs, kLeft)
              : hasR ? px0 + pw - cs.right.resolve(pw) - w - marginPx(cs, kRight)
                     : px0 + (pw - w) * 0.5f;
      layout(c, x, 0, w, ph, h);
      const float hh = c.rect.w;
      float y = hasT ? py0 + cs.top.resolve(ph) + marginPx(cs, kTop)
              : hasB ? py0 + ph - cs.bottom.resolve(ph) - hh - marginPx(cs, kBottom)
                     : py0 + (ph - hh) * 0.5f;
      translate(c, 0, y);
    }
  }
};

}  // namespace

std::unique_ptr<LayoutBox> layoutDocument(const UINode& root, const Stylesheet& sheet,
                                          glm::vec2 viewport, LayoutMetrics& metrics) {
  Layouter layouter(sheet, viewport, metrics);
  auto box = layouter.build(root, computeStyle(root, nullptr, sheet, viewport));
  layouter.layout(*box, 0.0f, 0.0f, viewport.x, viewport.y, viewport.y);
  return box;
}
