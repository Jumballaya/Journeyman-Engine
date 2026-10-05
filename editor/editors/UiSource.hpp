#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ui/HtmlParser.hpp"

// Edits to a .ui.html file's text that touch only the element concerned, so
// the author's formatting, comments and everything else stay as written.
// Elements are found by path: child-element indices from the root, which
// survive edits elsewhere (byte offsets don't).
namespace uisource {

using Path = std::vector<int>;

// The element at `path` in `doc`, or null.
const UINode* find(const ParsedHtml& doc, const Path& path);
// The path of `node` (an element of `doc`).
Path pathOf(const UINode& node);
// Element children only (text nodes skipped).
std::vector<const UINode*> elements(const UINode& node);

// An attribute set (or removed, with nullopt) in the element's open tag.
std::string setAttribute(const std::string& html, const UINode& node, const std::string& name,
                         const std::optional<std::string>& value);
// One property of the inline style set, or removed when `value` is empty.
std::string setStyle(const std::string& html, const UINode& node, const std::string& property, const std::string& value);
// The inline style as property → value, in order of appearance.
std::vector<std::pair<std::string, std::string>> inlineStyle(const UINode& node);
// Replaces an element's content with text (escaped); for elements without child elements.
std::string setText(const std::string& html, const UINode& node, const std::string& text);
// The element's text as written (entities decoded), when it has no child elements.
std::optional<std::string> textOf(const UINode& node);

// Adds `snippet` as the last child of `parent`, indented to match. Returns the new html.
std::string appendChild(const std::string& html, const UINode& parent, const std::string& snippet);
// Adds `snippet` right after `node`: on the next line when it has its own line, else inline.
std::string insertAfter(const std::string& html, const UINode& node, const std::string& snippet);
std::string remove(const std::string& html, const UINode& node);
std::string duplicate(const std::string& html, const UINode& node);
// Swaps the element with its previous (delta -1) or next (+1) sibling element.
std::string move(const std::string& html, const UINode& node, int delta);

// Class names the document's stylesheets define (".name" selectors), for suggestions.
std::vector<std::string> classesIn(const std::string& css);

}  // namespace uisource
