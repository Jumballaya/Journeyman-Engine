#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Dom.hpp"

// Forgiving HTML subset: unknown tags are boxes, unclosed tags close with their
// parent, <style> text is collected, <head>/<script>/<title> are dropped.
struct ParsedHtml {
  std::unique_ptr<UINode> root;  // synthetic "root" element holding the body
  std::string css;
};

ParsedHtml parseHtml(std::string_view html);
