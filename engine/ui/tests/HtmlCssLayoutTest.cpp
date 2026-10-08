#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

#include "HtmlParser.hpp"
#include "Layout.hpp"
#include "Style.hpp"
#include "UIDocument.hpp"

namespace {

// Every character is fontSize/2 wide; images are 32x16.
class FakeMetrics : public LayoutMetrics {
 public:
  float textWidth(const ComputedStyle& s, std::string_view text) override {
    return static_cast<float>(text.size()) * s.fontSize * 0.5f;
  }
  glm::vec2 imageSize(const std::string&) override { return {32, 16}; }
};

const LayoutBox* findBox(const LayoutBox& box, const std::string& id) {
  if (box.node && box.node->id == id) return &box;
  for (const auto& c : box.children) {
    if (const LayoutBox* f = findBox(*c, id)) return f;
  }
  return nullptr;
}

std::unique_ptr<LayoutBox> layout(const std::string& html, glm::vec2 viewport = {400, 300}) {
  static FakeMetrics metrics;
  ParsedHtml parsed = parseHtml(html);
  static std::vector<std::shared_ptr<UINode>> keepAlive;  // boxes point into the DOM
  static std::vector<std::shared_ptr<Stylesheet>> sheets;
  keepAlive.push_back(std::shared_ptr<UINode>(std::move(parsed.root)));
  auto sheet = std::make_shared<Stylesheet>();
  sheet->append(parsed.css);
  sheets.push_back(sheet);
  return layoutDocument(*keepAlive.back(), *sheet, viewport, metrics);
}

}  // namespace

TEST(HtmlParser, BuildsTreeWithAttributesEntitiesAndStyle) {
  ParsedHtml p = parseHtml(R"(<!doctype html><html><head><style>.a{color:red}</style></head>
    <body><div id="x" class="a  b" data-k=v>Fish &amp; chips<br/><img src='i.png'></div><!-- c --></body></html>)");
  ASSERT_EQ(p.root->children.size(), 1u);
  const UINode& div = *p.root->children[0];
  EXPECT_EQ(div.tag, "div");
  EXPECT_EQ(div.id, "x");
  EXPECT_EQ(div.classes, (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(div.attributes.at("data-k"), "v");
  ASSERT_EQ(div.children.size(), 3u);
  EXPECT_EQ(div.children[0]->text, "Fish & chips");
  EXPECT_EQ(div.children[1]->tag, "br");
  EXPECT_EQ(div.children[2]->attributes.at("src"), "i.png");
  EXPECT_NE(p.css.find(".a{color:red}"), std::string::npos);
}

TEST(HtmlParser, ToleratesUnclosedAndStrayTags) {
  ParsedHtml p = parseHtml("<div><p>one</span><p>two</div></b>");
  ASSERT_EQ(p.root->children.size(), 1u);
  EXPECT_EQ(p.root->children[0]->tag, "div");
}

TEST(HtmlParser, RecordsSourceRangesForEditing) {
  const std::string html = "<div id=\"a\">Hi <b>there</b><img src=\"x.png\"><p>open</div>";
  ParsedHtml parsed = parseHtml(html);
  const UINode& div = *parsed.root->children[0];
  auto text = [&](size_t from, size_t to) { return html.substr(from, to - from); };
  EXPECT_EQ(text(div.source.start, div.source.openEnd), "<div id=\"a\">");
  EXPECT_EQ(text(div.source.openEnd, div.source.closeStart), "Hi <b>there</b><img src=\"x.png\"><p>open");
  EXPECT_EQ(div.source.end, html.size());
  const UINode& greeting = *div.children[0];
  EXPECT_EQ(text(greeting.source.start, greeting.source.openEnd), "Hi ");
  const UINode& bold = *div.children[1];
  EXPECT_EQ(text(bold.source.start, bold.source.end), "<b>there</b>");
  const UINode& img = *div.children[2];
  EXPECT_EQ(text(img.source.start, img.source.end), "<img src=\"x.png\">");  // void: no content
  const UINode& p = *div.children[3];
  EXPECT_EQ(text(p.source.start, p.source.end), "<p>open");  // closed by its parent
}

TEST(Css, CascadeHonorsSpecificityOrderAndInline) {
  ParsedHtml p = parseHtml(R"(<div class="menu"><p id="item" class="sel" style="font-size: 20px">x</p></div>)");
  Stylesheet sheet;
  sheet.append(R"(
    #item { color: #00ff00; }
    .menu .sel { color: red; font-size: 10px; }
    p { color: blue; }
    div > p.sel { letter-spacing: 2px; }
    p:hover { color: white; }  /* unsupported: ignored */
  )");
  const UINode& item = *p.root->children[0]->children[0];
  ComputedStyle parent = computeStyle(*p.root->children[0], nullptr, sheet, {400, 300});
  ComputedStyle s = computeStyle(item, &parent, sheet, {400, 300});
  EXPECT_EQ(s.color, glm::vec4(0, 1, 0, 1));  // id beats classes
  EXPECT_FLOAT_EQ(s.fontSize, 20.0f);          // inline beats sheet
  EXPECT_FLOAT_EQ(s.letterSpacing, 2.0f);      // child combinator matched
}

TEST(Css, ParsesColorsAndLengths) {
  EXPECT_EQ(parseColor("#fff").value(), glm::vec4(1, 1, 1, 1));
  EXPECT_NEAR(parseColor("rgba(255, 0, 0, 0.5)")->a, 0.5f, 1e-5);
  EXPECT_EQ(parseColor("transparent")->a, 0.0f);
  EXPECT_FALSE(parseColor("#12").has_value());
  EXPECT_FLOAT_EQ(parseLength("50%", {0, 0})->resolve(200), 100.0f);
  EXPECT_FLOAT_EQ(parseLength("10vw", {400, 300})->value, 40.0f);
  EXPECT_TRUE(parseLength("auto", {0, 0})->isAuto());
}

TEST(Layout, BlockStacksChildrenWithMarginsAndPadding) {
  auto root = layout(R"(<style>div{height:20px} #b{margin-top:5px}</style>
    <section style="padding:10px"><div id="a"></div><div id="b"></div></section>)");
  const LayoutBox* a = findBox(*root, "a");
  const LayoutBox* b = findBox(*root, "b");
  ASSERT_TRUE(a && b);
  EXPECT_EQ(a->rect, glm::vec4(10, 10, 380, 20));
  EXPECT_EQ(b->rect, glm::vec4(10, 35, 380, 20));
}

TEST(Layout, FlexRowJustifiesAndCentersItems) {
  auto root = layout(R"(<div style="display:flex; justify-content:space-between; align-items:center; height:100px">
    <div id="l" style="width:50px; height:20px"></div><div id="r" style="width:30px; height:40px"></div></div>)");
  EXPECT_EQ(findBox(*root, "l")->rect, glm::vec4(0, 40, 50, 20));
  EXPECT_EQ(findBox(*root, "r")->rect, glm::vec4(370, 30, 30, 40));
}

TEST(Layout, FlexColumnCentersAndGrows) {
  auto root = layout(R"(<div style="display:flex; flex-direction:column; height:300px">
    <div id="top" style="height:50px"></div><div id="fill" style="flex:1"></div></div>)");
  EXPECT_EQ(findBox(*root, "top")->rect, glm::vec4(0, 0, 400, 50));
  EXPECT_EQ(findBox(*root, "fill")->rect, glm::vec4(0, 50, 400, 250));
}

// Every text piece under `box`, in tree order.
std::vector<TextPiece> textUnder(const LayoutBox& box) {
  std::vector<TextPiece> out = box.text;
  for (const auto& c : box.children) {
    auto more = textUnder(*c);
    out.insert(out.end(), more.begin(), more.end());
  }
  return out;
}

TEST(Layout, FlexRowKeepsTextBesideElements) {
  // The HUD idiom: the label is an anonymous flex item before the span.
  auto root = layout(R"(<div id="hud" style="display:flex; gap:8px; font-size:16px">
    SCORE <span id="score">0</span></div>)");
  const auto text = textUnder(*findBox(*root, "hud"));
  ASSERT_EQ(text.size(), 2u);
  EXPECT_EQ(text[0].text, "SCORE");
  EXPECT_FLOAT_EQ(text[0].x, 0);
  EXPECT_EQ(text[1].text, "0");
  EXPECT_FLOAT_EQ(text[1].x, 5 * 8 + 8);  // after "SCORE" (no trailing space) and the gap
  EXPECT_FLOAT_EQ(findBox(*root, "score")->rect.x, 48);
}

TEST(Layout, FlexContainerOfOnlyTextCentersIt) {
  auto root = layout(R"(<button id="b" style="display:flex; justify-content:center; align-items:center;
    width:100px; height:40px; font-size:16px">Play</button>)");
  const LayoutBox* b = findBox(*root, "b");
  const auto text = textUnder(*b);
  ASSERT_EQ(text.size(), 1u);
  EXPECT_EQ(text[0].text, "Play");
  EXPECT_FLOAT_EQ(text[0].x, (100 - 32) / 2.0f);
  EXPECT_GT(text[0].lineTop, 0.0f);  // centered vertically, not stuck at the top
}

TEST(Layout, FlexDropsWhitespaceBetweenItems) {
  auto root = layout(R"(<div id="row" style="display:flex">
    <span>a</span>
    <span>b</span>
  </div>)");
  EXPECT_EQ(findBox(*root, "row")->children.size(), 2u);
}

TEST(Layout, TextWrapsAndAligns) {
  // 16px font → 8px per char; 10 chars fit in 80px.
  auto root = layout(R"(<p id="t" style="width:80px; font-size:16px; text-align:right">aaaa bbbb cc</p>)");
  const LayoutBox* t = findBox(*root, "t");
  ASSERT_EQ(t->text.size(), 2u);
  EXPECT_EQ(t->text[0].text, "aaaa bbbb");
  EXPECT_FLOAT_EQ(t->text[0].x, 80 - 72);  // right-aligned
  EXPECT_EQ(t->text[1].text, "cc");
  EXPECT_FLOAT_EQ(t->text[1].lineTop, 20.0f);  // line-height 1.25 × 16
}

TEST(Layout, InlineSpansFormRunsOnOneLine) {
  auto root = layout(R"(<div id="d" style="font-size:16px">Score: <span id="s" style="color:#f00">42</span><br>next</div>)");
  const LayoutBox* d = findBox(*root, "d");
  ASSERT_EQ(d->text.size(), 3u);
  EXPECT_EQ(d->text[0].text, "Score: ");
  EXPECT_EQ(d->text[1].text, "42");
  EXPECT_FLOAT_EQ(d->text[1].x, 7 * 8.0f);  // after "Score: "
  EXPECT_EQ(d->text[1].style->color, glm::vec4(1, 0, 0, 1));
  EXPECT_FLOAT_EQ(d->text[2].lineTop, 20.0f);
}

TEST(Layout, AbsolutePositioningAndDisplayNone) {
  auto root = layout(R"(<div id="hud" style="position:absolute; right:10px; bottom:10px; width:40px; height:20px"></div>
    <div id="gone" style="display:none"></div>)");
  EXPECT_EQ(findBox(*root, "hud")->rect, glm::vec4(350, 270, 40, 20));
  EXPECT_EQ(findBox(*root, "gone"), nullptr);
}

TEST(UIDocument, MutationsRelayout) {
  ParsedHtml parsed = parseHtml(R"(<style>.hidden{display:none}</style><p id="a">x</p><p id="b">y</p>)");
  auto sheet = std::make_shared<Stylesheet>();
  sheet->append(parsed.css);
  UITemplate tmpl{std::shared_ptr<const UINode>(std::move(parsed.root)), sheet};
  UIDocument doc(tmpl);
  FakeMetrics m;

  EXPECT_NE(findBox(doc.layout({400, 300}, m), "a"), nullptr);
  EXPECT_TRUE(doc.setClass("a", "hidden", true));
  EXPECT_EQ(findBox(doc.layout({400, 300}, m), "a"), nullptr);
  EXPECT_TRUE(doc.setText("b", "hello"));
  EXPECT_EQ(findBox(doc.layout({400, 300}, m), "b")->text[0].text, "hello");
  EXPECT_TRUE(doc.setStyle("b", "opacity", "0.5"));
  EXPECT_FLOAT_EQ(findBox(doc.layout({400, 300}, m), "b")->style.opacity, 0.5f);
  EXPECT_TRUE(doc.setStyle("b", "opacity", ""));
  EXPECT_FLOAT_EQ(findBox(doc.layout({400, 300}, m), "b")->style.opacity, 1.0f);
  EXPECT_FALSE(doc.setText("missing", "z"));
}

TEST(Css, ImportantBeatsSpecificityAndInline) {
  ParsedHtml p = parseHtml(R"(<div id="box" class="hidden" style="display: flex">x</div>)");
  Stylesheet sheet;
  sheet.append("#box { display: block; } .hidden { display: none !important; }");
  ComputedStyle s = computeStyle(*p.root->children[0], nullptr, sheet, {400, 300});
  EXPECT_EQ(s.display, Display::None);
}

TEST(Css, LineHeightUnits) {
  ParsedHtml p = parseHtml(R"(<p id="a" style="font-size: 10px; line-height: 2em"></p>
    <p id="b" style="font-size: 10px; line-height: 15px"></p><p id="c" style="line-height: auto"></p>)");
  Stylesheet sheet;
  auto style = [&](size_t i) { return computeStyle(*p.root->children[i], nullptr, sheet, {400, 300}); };
  EXPECT_FLOAT_EQ(style(0).lineHeight, 2.0f);   // em is a multiple, not 32px
  EXPECT_FLOAT_EQ(style(1).lineHeight, 1.5f);
  EXPECT_FLOAT_EQ(style(2).lineHeight, 1.25f);  // unparsable: default kept
}

TEST(Layout, ShrinkToFitUsesWidestLine) {
  // 16px font → 8px per char; the widest line is "abcd" (32px), not all 6 chars.
  auto root = layout(R"(<div id="d" style="position:absolute; left:0; top:0; font-size:16px">abcd<br>ef</div>)");
  EXPECT_FLOAT_EQ(findBox(*root, "d")->rect.z, 32.0f);
}

TEST(UIDocument, EmptyIdMatchesNothing) {
  ParsedHtml parsed = parseHtml(R"(<p>keep</p>)");
  UITemplate tmpl{std::shared_ptr<const UINode>(std::move(parsed.root)), std::make_shared<Stylesheet>()};
  UIDocument doc(tmpl);
  EXPECT_FALSE(doc.has(""));
  EXPECT_FALSE(doc.setText("", "gone"));
}

// Not a test: layout time against flex nesting depth.
// Run with --gtest_also_run_disabled_tests --gtest_filter=*FlexDepthCost* (optimized build).
TEST(Layout, DISABLED_FlexDepthCost) {
  for (int depth : {2, 4, 6, 8, 10, 12}) {
    std::string html;
    for (int d = 0; d < depth; ++d) html += std::string("<div style=\"display:flex; flex-direction:") + (d % 2 ? "column" : "row") + "\">";
    html += "<span>leaf</span>";
    for (int d = 0; d < depth; ++d) html += "</div>";
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) layout(html);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 20;
    std::printf("BENCH layout_flex_depth_%d %.2f us\n", depth, us);
  }
}
