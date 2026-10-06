#include "UiSource.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace uisource {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isBlank(char c) { return c == ' ' || c == '\t'; }

std::string escape(const std::string& s, bool attribute) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (attribute && c == '"') out += "&quot;";
    else if (!attribute && c == '<') out += "&lt;";
    else if (!attribute && c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

// Where the spaces and tabs just before `at` begin.
size_t blankStart(const std::string& html, size_t at) {
  while (at > 0 && isBlank(html[at - 1])) --at;
  return at;
}

bool startsLine(const std::string& html, size_t at) {
  at = blankStart(html, at);
  return at == 0 || html[at - 1] == '\n';
}

// The whitespace a line starts with, for the line holding `at`.
std::string indentAt(const std::string& html, size_t at) {
  size_t line = html.rfind('\n', at == 0 ? 0 : at - 1);
  line = line == std::string::npos ? 0 : line + 1;
  size_t end = line;
  while (end < html.size() && isBlank(html[end])) ++end;
  return html.substr(line, end - line);
}

std::string spanOf(const std::string& html, const UINode& node) {
  return html.substr(node.source.start, node.source.end - node.source.start);
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

}  // namespace

std::vector<const UINode*> elements(const UINode& node) {
  std::vector<const UINode*> out;
  for (const auto& c : node.children) {
    if (!c->isText()) out.push_back(c.get());
  }
  return out;
}

std::vector<const UINode*> allElements(const UINode& node) {
  std::vector<const UINode*> out;
  for (const UINode* c : elements(node)) {
    out.push_back(c);
    const auto below = allElements(*c);
    out.insert(out.end(), below.begin(), below.end());
  }
  return out;
}

const UINode* find(const ParsedHtml& doc, const Path& path) {
  const UINode* n = doc.root.get();
  if (!n) return nullptr;
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

std::string pathText(const Path& path) {
  std::string out;
  for (int i : path) out += (out.empty() ? "" : "/") + std::to_string(i);
  return out;
}

Path parsePath(const std::string& text) {
  Path path;
  for (size_t at = 0; at < text.size();) {
    const size_t end = std::min(text.find('/', at), text.size());
    path.push_back(std::atoi(text.substr(at, end - at).c_str()));
    at = end + 1;
  }
  return path;
}

std::string setAttribute(const std::string& html, const UINode& node, const std::string& name,
                         const std::optional<std::string>& value) {
  if (node.parent == nullptr) return html;  // the root has no tag
  size_t insertAt = 0;
  const auto spans = attributeSpans(html, node, insertAt);
  auto it = std::find_if(spans.begin(), spans.end(), [&](const AttributeSpan& a) { return a.name == name; });
  const std::string written = value ? name + "=\"" + escape(*value, true) + "\"" : "";
  std::string out = html;
  if (it != spans.end()) {
    size_t start = it->start;
    while (!value && start > node.source.start && isSpace(out[start - 1])) --start;  // and the space before it
    out.replace(start, it->end - start, written);
  } else if (value) {
    out.insert(insertAt, " " + written);
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> inlineStyle(const UINode& node) {
  auto trim = [](std::string v) {
    while (!v.empty() && isSpace(v.front())) v.erase(v.begin());
    while (!v.empty() && isSpace(v.back())) v.pop_back();
    return v;
  };
  std::vector<std::pair<std::string, std::string>> out;
  const std::string& s = node.inlineStyle;
  for (size_t start = 0; start < s.size();) {
    const size_t end = std::min(s.find(';', start), s.size());
    const std::string decl = s.substr(start, end - start);
    start = end + 1;
    const size_t colon = decl.find(':');
    if (colon == std::string::npos) continue;
    std::string property = trim(decl.substr(0, colon));
    std::transform(property.begin(), property.end(), property.begin(), [](unsigned char c) { return std::tolower(c); });
    if (!property.empty()) out.emplace_back(property, trim(decl.substr(colon + 1)));
  }
  return out;
}

std::string setStyles(const std::string& html, const UINode& node, const std::vector<std::pair<std::string, std::string>>& properties) {
  auto style = inlineStyle(node);
  for (const auto& [property, value] : properties) {
    auto it = std::find_if(style.begin(), style.end(), [&](const auto& p) { return p.first == property; });
    if (it == style.end()) {
      if (!value.empty()) style.emplace_back(property, value);
    } else if (value.empty()) {
      style.erase(it);
    } else {
      it->second = value;
    }
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
  out.replace(node.source.openEnd, node.source.closeStart - node.source.openEnd, escape(text, false));
  return out;
}

std::string insert(const std::string& html, const UINode& target, const std::string& snippet, Place where) {
  std::string out = html;
  const UINode::Source& at = target.source;
  // A line break between inline siblings would add a space between their runs.
  const bool ownLine = startsLine(html, at.start);
  if (where == Place::Before) {
    out.insert(at.start, ownLine ? snippet + "\n" + indentAt(html, at.start) : snippet);
    return out;
  }
  if (where == Place::After) {
    out.insert(at.end, ownLine ? "\n" + indentAt(html, at.start) + snippet : snippet);
    return out;
  }
  if (const auto kids = elements(target); !kids.empty()) {
    const UINode& last = *kids.back();
    out.insert(last.source.end, "\n" + indentAt(html, last.source.start) + snippet);
    return out;
  }
  if (target.parent == nullptr) {  // an empty document
    out += (out.empty() || out.back() == '\n' ? "" : "\n") + snippet + "\n";
    return out;
  }
  // Inside an empty element: on its own line, one level in, unless it's a one-liner.
  const std::string content = html.substr(at.openEnd, at.closeStart - at.openEnd);
  if (std::all_of(content.begin(), content.end(), isSpace)) {
    const std::string indent = indentAt(html, at.start);
    out.replace(at.openEnd, content.size(), "\n" + indent + "  " + snippet + "\n" + indent);
  } else {
    out.insert(at.closeStart, snippet);
  }
  return out;
}

std::string remove(const std::string& html, const UINode& node) {
  if (node.parent == nullptr) return html;
  size_t start = node.source.start, end = node.source.end;
  // Take the whole line when the element is alone on it.
  size_t lineEnd = end;
  while (lineEnd < html.size() && isBlank(html[lineEnd])) ++lineEnd;
  if (startsLine(html, start) && (lineEnd == html.size() || html[lineEnd] == '\n' || html[lineEnd] == '\r')) {
    start = blankStart(html, start);
    end = std::min(lineEnd + 1, html.size());
  }
  std::string out = html;
  out.erase(start, end - start);
  return out;
}

std::string duplicate(const std::string& html, const UINode& node) {
  if (node.parent == nullptr) return html;
  // Ids must stay unique (scripts find elements by them): the copy's get a number.
  static const std::regex idAttribute(R"re((\s)id="([^"]*)")re");
  const std::string copy = spanOf(html, node);
  std::string renamed;
  auto taken = [&](const std::string& id) {
    const std::string attribute = "id=\"" + id + "\"";
    return html.find(attribute) != std::string::npos || renamed.find(attribute) != std::string::npos;
  };
  auto last = copy.cbegin();
  for (auto it = std::sregex_iterator(copy.begin(), copy.end(), idAttribute); it != std::sregex_iterator(); ++it) {
    std::string base = (*it)[2];
    if (const size_t dash = base.find_last_of('-'); dash != std::string::npos && dash + 1 < base.size() &&
        base.find_first_not_of("0123456789", dash + 1) == std::string::npos) {
      base.resize(dash);
    }
    std::string id;
    for (int n = 2; taken(id = base + "-" + std::to_string(n)); ++n) {}
    renamed.append(last, (*it)[0].first);
    renamed += (*it)[1].str() + "id=\"" + id + "\"";
    last = (*it)[0].second;
  }
  renamed.append(last, copy.cend());
  return insert(html, node, renamed, Place::After);
}

std::string move(const std::string& html, const UINode& node, int delta) {
  if (!node.parent) return html;
  const auto siblings = elements(*node.parent);
  const long at = (std::find(siblings.begin(), siblings.end(), &node) - siblings.begin()) + delta;
  if (at < 0 || at >= static_cast<long>(siblings.size())) return html;
  const UINode& other = *siblings[static_cast<size_t>(at)];
  const UINode& first = delta < 0 ? other : node;
  const UINode& second = delta < 0 ? node : other;
  const std::string between = html.substr(first.source.end, second.source.start - first.source.end);
  std::string out = html;
  out.replace(first.source.start, second.source.end - first.source.start, spanOf(html, second) + between + spanOf(html, first));
  return out;
}

std::string relocate(const std::string& html, const UINode& node, const UINode& target, Place where, size_t* moved) {
  for (const UINode* t = &target; t; t = t->parent) {
    if (t == &node) return html;  // into itself
  }
  if (where == Place::Inside && target.source.openEnd == target.source.end && target.parent) return html;  // a void element
  const std::string snippet = spanOf(html, node);
  const auto kids = elements(target);
  const size_t at = where == Place::Before ? target.source.start
                    : where == Place::After ? target.source.end
                    : kids.empty()          ? target.source.openEnd
                                            : kids.back()->source.end;
  // Edit the later spot first, so the earlier one's offsets still hold.
  const bool later = at >= node.source.end;
  const std::string out = later ? remove(insert(html, target, snippet, where), node) : insert(remove(html, node), target, snippet, where);
  if (moved) {
    // The copy of its text nearest where it went (the removal shifted later offsets back).
    const size_t expected = later ? at - snippet.size() : at;
    const auto distance = [&](size_t p) { return p > expected ? p - expected : expected - p; };
    size_t best = std::string::npos;
    for (size_t f = out.find(snippet); f != std::string::npos; f = out.find(snippet, f + 1)) {
      if (best == std::string::npos || distance(f) < distance(best)) best = f;
    }
    *moved = best;
  }
  return out;
}

std::string inlineStylesheets(const std::string& html, const std::function<std::string(const std::string&)>& read) {
  const ParsedHtml doc = parseHtml(html);
  const auto all = allElements(*doc.root);
  std::string out = html;
  // Last first, so earlier offsets hold.
  for (auto it = all.rbegin(); it != all.rend(); ++it) {
    const UINode& link = **it;
    if (link.tag != "link" || !link.attributes.contains("href")) continue;
    const std::string css = read(link.attributes.at("href"));
    out = setAttribute(out, link, "href", std::nullopt);
    out.insert(link.source.start, "<style>" + css + "</style>");
  }
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
