#pragma once

#include <functional>
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
// A path as "0/2/1", and back.
std::string pathText(const Path& path);
Path parsePath(const std::string& text);
// Element children only (text nodes skipped).
std::vector<const UINode*> elements(const UINode& node);
// Every element under `node`, in document order.
std::vector<const UINode*> allElements(const UINode& node);

// An attribute set (or removed, with nullopt) in the element's open tag.
std::string setAttribute(const std::string& html, const UINode& node, const std::string& name,
                         const std::optional<std::string>& value);
// Inline style properties set, or removed when the value is empty (one edit to the tag).
std::string setStyles(const std::string& html, const UINode& node, const std::vector<std::pair<std::string, std::string>>& properties);
// The inline style as property → value, in order of appearance.
std::vector<std::pair<std::string, std::string>> inlineStyle(const UINode& node);
// Replaces an element's content with text (escaped); for elements without child elements.
std::string setText(const std::string& html, const UINode& node, const std::string& text);
// The element's text as written (entities decoded), when it has no child elements.
std::optional<std::string> textOf(const UINode& node);

// Before/After `target`: on the next line when it has its own line, else
// inline. Inside: as the last child, indented to match.
enum class Place { Before, After, Inside };
std::string insert(const std::string& html, const UINode& target, const std::string& snippet, Place where);
std::string remove(const std::string& html, const UINode& node);
std::string duplicate(const std::string& html, const UINode& node);
// Swaps the element with its previous (delta -1) or next (+1) sibling element.
std::string move(const std::string& html, const UINode& node, int delta);
// Moves an element to `where` around `target`. Moving it into itself or a
// descendant leaves the html as it was. `moved` gets the element's offset in
// the result, to find it again.
std::string relocate(const std::string& html, const UINode& node, const UINode& target, Place where, size_t* moved = nullptr);

// The html with each linked stylesheet's text inlined (read through `read`,
// by href) and the <link> left in place without its href, so element paths
// don't change. For previews that shouldn't load sheets from a build.
std::string inlineStylesheets(const std::string& html, const std::function<std::string(const std::string&)>& read);

// Class names the document's stylesheets define (".name" selectors), for suggestions.
std::vector<std::string> classesIn(const std::string& css);

}  // namespace uisource
