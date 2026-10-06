#include "HtmlParser.hpp"

#include <cstdlib>
#include <unordered_map>

#include "Text.hpp"

namespace {

bool isVoid(const std::string& tag) {
  static const char* kVoid[] = {"img", "br", "hr", "input", "meta", "link", "source", "wbr"};
  return std::any_of(std::begin(kVoid), std::end(kVoid), [&](const char* v) { return tag == v; });
}

// Decodes entities. Non-breaking spaces become U+00A0 so whitespace
// collapsing keeps them.
std::string decodeEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    const size_t semi = s.find(';', i);
    if (semi == std::string_view::npos || semi - i > 10) {
      out += '&';
      continue;
    }
    static const std::unordered_map<std::string_view, uint32_t> kNamed = {
        {"amp", '&'},      {"lt", '<'},      {"gt", '>'},       {"quot", '"'},      {"apos", '\''},
        {"nbsp", 0xA0},    {"copy", 0xA9},   {"times", 0xD7},   {"middot", 0xB7},   {"hellip", 0x2026},
        {"larr", 0x2190},  {"rarr", 0x2192}, {"uarr", 0x2191},  {"darr", 0x2193},
    };
    const std::string_view name = s.substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (!name.empty() && name[0] == '#') {
      const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      cp = static_cast<uint32_t>(std::strtoul(std::string(name.substr(hex ? 2 : 1)).c_str(), nullptr, hex ? 16 : 10));
    } else if (auto it = kNamed.find(name); it != kNamed.end()) {
      cp = it->second;
    }
    if (cp == 0) {
      out += '&';
      continue;
    }
    appendUtf8(out, cp);
    i = semi;
  }
  return out;
}

// Collapses runs of HTML whitespace to one space (keeps U+00A0).
std::string collapseWhitespace(std::string_view s) {
  std::string out;
  bool lastSpace = false;
  for (char c : s) {
    if (isSpace(c)) {
      if (!lastSpace) out += ' ';
      lastSpace = true;
    } else {
      out += c;
      lastSpace = false;
    }
  }
  return out;
}

class Parser {
 public:
  explicit Parser(std::string_view src) : _s(src) {}

  ParsedHtml parse() {
    ParsedHtml result;
    result.root = std::make_unique<UINode>();
    result.root->tag = "root";
    UINode* current = result.root.get();

    while (_i < _s.size()) {
      if (_s[_i] != '<') {
        readText(*current);
        continue;
      }
      if (startsWith("<!--")) {
        const size_t end = _s.find("-->", _i + 4);
        _i = end == std::string_view::npos ? _s.size() : end + 3;
        continue;
      }
      if (startsWith("<!")) {  // doctype
        skipPast('>');
        continue;
      }
      if (startsWith("</")) {
        const size_t closeStart = _i;
        _i += 2;
        const std::string name = lower(readName());
        skipPast('>');
        // Close up to the matching open element; ignore stray end tags. Elements
        // left open inside it end where its close tag starts.
        UINode* match = nullptr;
        for (UINode* n = current; n && n != result.root.get(); n = n->parent) {
          if (n->tag == name) {
            match = n;
            break;
          }
        }
        if (match) {
          for (UINode* n = current; n != match; n = n->parent) n->source.closeStart = n->source.end = closeStart;
          match->source.closeStart = closeStart;
          match->source.end = _i;
          current = match->parent;
        }
        continue;
      }
      const size_t tagStart = _i;
      ++_i;  // '<'
      const std::string name = lower(readName());
      if (name.empty()) {  // a literal '<'
        current->appendChild(textNode("<")).source = {tagStart, _i, _i, _i};
        continue;
      }
      auto node = std::make_unique<UINode>();
      node->tag = name;
      const bool selfClosing = readAttributes(*node);
      node->source = {tagStart, _i, _i, _i};  // a void element ends with its open tag

      if (name == "style" || name == "script" || name == "title") {
        const std::string closing = "</" + name;
        size_t end = findCaseInsensitive(closing, _i);
        if (end == std::string_view::npos) end = _s.size();
        if (name == "style") {
          result.css.append(_s.substr(_i, end - _i));
          result.css += '\n';
        }
        _i = end;
        skipPast('>');
        continue;
      }
      if (name == "html" || name == "body" || name == "head") continue;  // transparent

      UINode& added = current->appendChild(std::move(node));
      if (!selfClosing && !isVoid(name)) current = &added;
    }
    for (UINode* n = current; n; n = n->parent) n->source.closeStart = n->source.end = _s.size();  // left open
    result.root->source = {0, 0, _s.size(), _s.size()};
    trimTextNodes(*result.root);
    return result;
  }

 private:
  std::string_view _s;
  size_t _i = 0;

  bool startsWith(std::string_view p) const { return _s.substr(_i, p.size()) == p; }

  void skipPast(char c) {
    const size_t end = _s.find(c, _i);
    _i = end == std::string_view::npos ? _s.size() : end + 1;
  }

  size_t findCaseInsensitive(std::string_view lowerNeedle, size_t from) const {
    auto it = std::search(_s.begin() + from, _s.end(), lowerNeedle.begin(), lowerNeedle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
    return it == _s.end() ? std::string_view::npos : static_cast<size_t>(it - _s.begin());
  }

  std::string readName() {
    const size_t start = _i;
    while (_i < _s.size() && (std::isalnum(static_cast<unsigned char>(_s[_i])) || _s[_i] == '-' || _s[_i] == '_' || _s[_i] == ':')) ++_i;
    return std::string(_s.substr(start, _i - start));
  }

  void skipSpace() {
    while (_i < _s.size() && isSpace(_s[_i])) ++_i;
  }

  // Returns true for "<tag ... />".
  bool readAttributes(UINode& node) {
    while (_i < _s.size()) {
      skipSpace();
      if (_i >= _s.size()) return false;
      if (_s[_i] == '>') {
        ++_i;
        return false;
      }
      if (startsWith("/>")) {
        _i += 2;
        return true;
      }
      const std::string name = lower(readName());
      if (name.empty()) {  // garbage; skip a char
        ++_i;
        continue;
      }
      std::string value;
      skipSpace();
      if (_i < _s.size() && _s[_i] == '=') {
        ++_i;
        skipSpace();
        if (_i < _s.size() && (_s[_i] == '"' || _s[_i] == '\'')) {
          const char q = _s[_i++];
          const size_t end = std::min(_s.find(q, _i), _s.size());
          value = std::string(_s.substr(_i, end - _i));
          _i = std::min(end + 1, _s.size());
        } else {
          const size_t start = _i;
          while (_i < _s.size() && !isSpace(_s[_i]) && _s[_i] != '>') ++_i;
          value = std::string(_s.substr(start, _i - start));
        }
      }
      value = decodeEntities(value);
      if (name == "id") {
        node.id = value;
      } else if (name == "class") {
        for (std::string_view rest = trim(value); !rest.empty(); rest = trim(rest)) {
          const size_t end = std::find_if(rest.begin(), rest.end(), isSpace) - rest.begin();
          node.classes.emplace_back(rest.substr(0, end));
          rest.remove_prefix(end);
        }
      } else if (name == "style") {
        node.inlineStyle = value;
      }
      node.attributes[name] = value;
    }
    return false;
  }

  std::unique_ptr<UINode> textNode(std::string text) {
    auto n = std::make_unique<UINode>();
    n->text = std::move(text);
    return n;
  }

  void readText(UINode& parent) {
    const size_t start = _i;
    while (_i < _s.size() && _s[_i] != '<') ++_i;
    std::string text = collapseWhitespace(decodeEntities(_s.substr(start, _i - start)));
    if (!text.empty()) parent.appendChild(textNode(std::move(text))).source = {start, _i, _i, _i};
  }

  // Drops whitespace-only text nodes but keeps edge spaces of real text, which
  // separate inline runs ("Score: <span>0</span>").
  static void trimTextNodes(UINode& node) {
    auto& kids = node.children;
    kids.erase(std::remove_if(kids.begin(), kids.end(),
                              [](const std::unique_ptr<UINode>& c) {
                                return c->isText() && c->text.find_first_not_of(' ') == std::string::npos;
                              }),
               kids.end());
    for (auto& c : kids) {
      if (!c->isText()) trimTextNodes(*c);
    }
  }
};

}  // namespace

ParsedHtml parseHtml(std::string_view html) {
  return Parser(html).parse();
}
