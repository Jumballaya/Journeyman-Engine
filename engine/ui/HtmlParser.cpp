#include "HtmlParser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

bool isVoid(const std::string& tag) {
  static const char* kVoid[] = {"img", "br", "hr", "input", "meta", "link", "source", "wbr"};
  return std::any_of(std::begin(kVoid), std::end(kVoid), [&](const char* v) { return tag == v; });
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
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
    const std::string_view name = s.substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (!name.empty() && name[0] == '#') {
      const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      cp = static_cast<uint32_t>(std::strtoul(std::string(name.substr(hex ? 2 : 1)).c_str(), nullptr, hex ? 16 : 10));
    } else if (name == "amp") cp = '&';
    else if (name == "lt") cp = '<';
    else if (name == "gt") cp = '>';
    else if (name == "quot") cp = '"';
    else if (name == "apos") cp = '\'';
    else if (name == "nbsp") cp = 0xA0;
    else if (name == "copy") cp = 0xA9;
    else if (name == "times") cp = 0xD7;
    else if (name == "middot") cp = 0xB7;
    else if (name == "hellip") cp = 0x2026;
    else if (name == "larr") cp = 0x2190;
    else if (name == "rarr") cp = 0x2192;
    else if (name == "uarr") cp = 0x2191;
    else if (name == "darr") cp = 0x2193;
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
        current->appendChild(textNode("<"));
        continue;
      }
      auto node = std::make_unique<UINode>();
      node->tag = name;
      bool selfClosing = readAttributes(*node);
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

  size_t findCaseInsensitive(const std::string& needle, size_t from) const {
    for (size_t p = from; p + needle.size() <= _s.size(); ++p) {
      if (lower(_s.substr(p, needle.size())) == needle) return p;
    }
    return std::string_view::npos;
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
          const size_t end = _s.find(q, _i);
          value = std::string(_s.substr(_i, (end == std::string_view::npos ? _s.size() : end) - _i));
          _i = end == std::string_view::npos ? _s.size() : end + 1;
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
        std::string cls;
        for (char c : value + " ") {
          if (isSpace(c)) {
            if (!cls.empty()) node.classes.push_back(cls);
            cls.clear();
          } else {
            cls += c;
          }
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
