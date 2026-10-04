#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Dom.hpp"

// Parses the HTML subset the UI system renders. Forgiving by design: unknown
// tags become generic boxes, unclosed tags are closed at the parent's end,
// stray end tags are ignored. Supported: elements + attributes (quoted or
// bare), void elements (img, br, hr, input, meta, link), comments, text with
// whitespace collapsing, the common named entities and numeric entities.
// <style> contents are returned verbatim (concatenated) instead of becoming
// nodes; <head>, <script> and <title> are dropped.
struct ParsedHtml {
  std::unique_ptr<UINode> root;  // synthetic "root" element holding the body
  std::string css;
};

ParsedHtml parseHtml(std::string_view html);
