#include "UiSource.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace uisource {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string escapeAttribute(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

std::string escapeText(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

// The whitespace a line starts with, for the line holding `at`.
std::string indentAt(const std::string& html, size_t at) {
  size_t line = html.rfind('\n', at == 0 ? 0 : at - 1);
  line = line == std::string::npos ? 0 : line + 1;
  size_t end = line;
  while (end < html.size() && (html[end] == ' ' || html[end] == '\t')) ++end;
  return html.substr(line, end - line);
}

// One attribute in an open tag: [start, end) spans `name="value"`.
struct AttributeSpan {
  std::string name;
  size_t start, end;
};

std::vector<AttributeSpan> attributeSpans(const std::string& html, const UINode& node, size_t& insertAt) {
  std::vector<AttributeSpan> out;
  size_t i = node.source.start + 1;
  const size_t stop = node.source.openEnd;
  while (i < stop && !isSpace(html[i]) && html[i] != '>' && html[i] != '/') ++i;  // the tag name
  insertAt = i;
  while (i < stop) {
    while (i < stop && isSpace(html[i])) ++i;
    if (i >= stop || html[i] == '>' || html[i] == '/') break;
    const size_t start = i;
    while (i < stop && !isSpace(html[i]) && html[i] != '=' && html[i] != '>' && html[i] != '/') ++i;
    std::string name = html.substr(start, i - start);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    size_t j = i;
    while (j < stop && isSpace(html[j])) ++j;
    if (j < stop && html[j] == '=') {
      ++j;
      while (j < stop && isSpace(html[j])) ++j;
      if (j < stop && (html[j] == '"' || html[j] == '\'')) {
        const size_t close = html.find(html[j], j + 1);
        i = close == std::string::npos || close >= stop ? stop : close + 1;
      } else {
        while (j < stop && !isSpace(html[j]) && html[j] != '>') ++j;
        i = j;
      }
    }
    out.push_back({name, start, i});
    insertAt = i;
  }
  return out;
}

const UINode* siblingElement(const UINode& node, int delta) {
  if (!node.parent) return nullptr;
  const auto siblings = elements(*node.parent);
  auto it = std::find(siblings.begin(), siblings.end(), &node);
  if (it == siblings.end()) return nullptr;
  const long at = (it - siblings.begin()) + delta;
  return at < 0 || at >= static_cast<long>(siblings.size()) ? nullptr : siblings[static_cast<size_t>(at)];
}

}  // namespace

std::vector<const UINode*> elements(const UINode& node) {
  std::vector<const UINode*> out;
  for (const auto& c : node.children) {
    if (!c->isText()) out.push_back(c.get());
  }
  return out;
}

const UINode* find(const ParsedHtml& doc, const Path& path) {
  const UINode* n = doc.root.get();
  for (int i : path) {
    const auto kids = elements(*n);
    if (i < 0 || i >= static_cast<int>(kids.size())) return nullptr;
    n = kids[static_cast<size_t>(i)];
  }
  return n;
}

Path pathOf(const UINode& node) {
  Path path;
  for (const UINode* n = &node; n->parent; n = n->parent) {
    const auto kids = elements(*n->parent);
    path.insert(path.begin(), static_cast<int>(std::find(kids.begin(), kids.end(), n) - kids.begin()));
  }
  return path;
}

std::string setAttribute(const std::string& html, const UINode& node, const std::string& name,
                         const std::optional<std::string>& value) {
  if (node.parent == nullptr) return html;  // the root has no tag
  size_t insertAt = 0;
  const auto spans = attributeSpans(html, node, insertAt);
  auto it = std::find_if(spans.begin(), spans.end(), [&](const AttributeSpan& a) { return a.name == name; });
  const std::string written = value ? name + "=\"" + escapeAttribute(*value) + "\"" : "";
  std::string out = html;
  if (it != spans.end()) {
    size_t start = it->start;
    if (!value) {
      while (start > node.source.start && isSpace(out[start - 1])) --start;  // and the space before it
    }
    out.replace(start, it->end - start, written);
  } else if (value) {
    out.insert(insertAt, " " + written);
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> inlineStyle(const UINode& node) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t start = 0;
  const std::string& s = node.inlineStyle;
  while (start < s.size()) {
    size_t end = s.find(';', start);
    if (end == std::string::npos) end = s.size();
    const std::string decl = s.substr(start, end - start);
    const size_t colon = decl.find(':');
    if (colon != std::string::npos) {
      auto trim = [](std::string v) {
        while (!v.empty() && isSpace(v.front())) v.erase(v.begin());
        while (!v.empty() && isSpace(v.back())) v.pop_back();
        return v;
      };
      std::string property = trim(decl.substr(0, colon));
      std::transform(property.begin(), property.end(), property.begin(), [](unsigned char c) { return std::tolower(c); });
      if (!property.empty()) out.emplace_back(property, trim(decl.substr(colon + 1)));
    }
    start = end + 1;
  }
  return out;
}

std::string setStyle(const std::string& html, const UINode& node, const std::string& property, const std::string& value) {
  auto style = inlineStyle(node);
  auto it = std::find_if(style.begin(), style.end(), [&](const auto& p) { return p.first == property; });
  if (value.empty()) {
    if (it != style.end()) style.erase(it);
  } else if (it != style.end()) {
    it->second = value;
  } else {
    style.emplace_back(property, value);
  }
  std::string written;
  for (const auto& [p, v] : style) written += (written.empty() ? "" : "; ") + p + ": " + v;
  return setAttribute(html, node, "style", written.empty() ? std::nullopt : std::optional(written));
}

std::optional<std::string> textOf(const UINode& node) {
  if (node.parent == nullptr || node.source.openEnd == node.source.end || !elements(node).empty()) return std::nullopt;
  std::string text;
  for (const auto& c : node.children) text += c->text;
  return text;
}

std::string setText(const std::string& html, const UINode& node, const std::string& text) {
  if (!textOf(node)) return html;
  std::string out = html;
  out.replace(node.source.openEnd, node.source.closeStart - node.source.openEnd, escapeText(text));
  return out;
}

std::string appendChild(const std::string& html, const UINode& parent, const std::string& snippet) {
  const auto kids = elements(parent);
  std::string out = html;
  if (!kids.empty()) {
    const UINode& last = *kids.back();
    out.insert(last.source.end, "\n" + indentAt(html, last.source.start) + snippet);
    return out;
  }
  if (parent.parent == nullptr) {  // an empty document
    out += (out.empty() || out.back() == '\n' ? "" : "\n") + snippet + "\n";
    return out;
  }
  // Inside an empty element: on its own line, one level in, unless it's a one-liner.
  const std::string indent = indentAt(html, parent.source.start);
  const size_t from = parent.source.openEnd, to = parent.source.closeStart;
  const std::string content = html.substr(from, to - from);
  const bool blank = std::all_of(content.begin(), content.end(), [](char c) { return isSpace(c); });
  if (blank) {
    out.replace(from, to - from, "\n" + indent + "  " + snippet + "\n" + indent);
  } else {
    out.insert(to, snippet);
  }
  return out;
}

std::string remove(const std::string& html, const UINode& node) {
  if (node.parent == nullptr) return html;
  size_t start = node.source.start, end = node.source.end;
  // Take the whole line when the element is alone on it.
  size_t lineStart = start;
  while (lineStart > 0 && (html[lineStart - 1] == ' ' || html[lineStart - 1] == '\t')) --lineStart;
  size_t lineEnd = end;
  while (lineEnd < html.size() && (html[lineEnd] == ' ' || html[lineEnd] == '\t')) ++lineEnd;
  if ((lineStart == 0 || html[lineStart - 1] == '\n') && (lineEnd == html.size() || html[lineEnd] == '\n' || html[lineEnd] == '\r')) {
    start = lineStart;
    end = lineEnd < html.size() ? lineEnd + 1 : lineEnd;
  }
  std::string out = html;
  out.erase(start, end - start);
  return out;
}

std::string duplicate(const std::string& html, const UINode& node) {
  if (node.parent == nullptr) return html;
  // Ids must stay unique (scripts find elements by them): the copy's get a number.
  static const std::regex idAttribute(R"re(\bid="([^"]*)")re");
  const std::string copy = html.substr(node.source.start, node.source.end - node.source.start);
  std::string renamed;
  auto last = copy.cbegin();
  for (auto it = std::sregex_iterator(copy.begin(), copy.end(), idAttribute); it != std::sregex_iterator(); ++it) {
    std::string base = (*it)[1];
    if (const size_t dash = base.find_last_of('-'); dash != std::string::npos && dash + 1 < base.size() &&
        base.find_first_not_of("0123456789", dash + 1) == std::string::npos) {
      base.resize(dash);
    }
    std::string id;
    for (int n = 2; html.find("id=\"" + (id = base + "-" + std::to_string(n)) + "\"") != std::string::npos; ++n) {}
    renamed.append(last, (*it)[0].first);
    renamed += "id=\"" + id + "\"";
    last = (*it)[0].second;
  }
  renamed.append(last, copy.cend());
  return insertAfter(html, node, renamed);
}

std::string insertAfter(const std::string& html, const UINode& node, const std::string& snippet) {
  // A line break between inline siblings would add a space between their runs.
  size_t lineStart = node.source.start;
  while (lineStart > 0 && (html[lineStart - 1] == ' ' || html[lineStart - 1] == '\t')) --lineStart;
  const bool ownLine = lineStart == 0 || html[lineStart - 1] == '\n';
  std::string out = html;
  out.insert(node.source.end, ownLine ? "\n" + indentAt(html, node.source.start) + snippet : snippet);
  return out;
}

std::string move(const std::string& html, const UINode& node, int delta) {
  const UINode* other = siblingElement(node, delta);
  if (!other) return html;
  const UINode& first = delta < 0 ? *other : node;
  const UINode& second = delta < 0 ? node : *other;
  auto span = [&](const UINode& n) { return html.substr(n.source.start, n.source.end - n.source.start); };
  const std::string between = html.substr(first.source.end, second.source.start - first.source.end);
  std::string out = html;
  out.replace(first.source.start, second.source.end - first.source.start, span(second) + between + span(first));
  return out;
}

std::vector<std::string> classesIn(const std::string& css) {
  static const std::regex selector(R"(\.([A-Za-z_][A-Za-z0-9_-]*))");
  std::set<std::string> names;
  // Only in selectors: skip declaration blocks.
  int depth = 0;
  std::string selectors;
  for (char c : css) {
    if (c == '{') ++depth, selectors += ' ';
    else if (c == '}') --depth;
    else if (depth == 0) selectors += c;
  }
  for (auto it = std::sregex_iterator(selectors.begin(), selectors.end(), selector); it != std::sregex_iterator(); ++it) names.insert((*it)[1]);
  return {names.begin(), names.end()};
}

}  // namespace uisource
