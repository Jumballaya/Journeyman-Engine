#include "UIDocument.hpp"

UIDocument::UIDocument(const UITemplate& tmpl) : _root(tmpl.root->clone()), _sheet(tmpl.sheet) {}

bool UIDocument::setText(const std::string& id, const std::string& text) {
  UINode* node = _root->findById(id);
  if (!node) return false;
  if (node->children.size() == 1 && node->children[0]->isText() && node->children[0]->text == text) return true;
  node->children.clear();
  auto t = std::make_unique<UINode>();
  t->text = text;
  node->appendChild(std::move(t));
  _dirty = true;
  return true;
}

bool UIDocument::setClass(const std::string& id, const std::string& cls, bool on) {
  UINode* node = _root->findById(id);
  if (!node) return false;
  auto it = std::find(node->classes.begin(), node->classes.end(), cls);
  if (on && it == node->classes.end()) {
    node->classes.push_back(cls);
    _dirty = true;
  } else if (!on && it != node->classes.end()) {
    node->classes.erase(it);
    _dirty = true;
  }
  return true;
}

bool UIDocument::setStyle(const std::string& id, const std::string& property, const std::string& value) {
  UINode* node = _root->findById(id);
  if (!node) return false;
  auto decls = parseDeclarations(node->inlineStyle);
  std::erase_if(decls, [&](const CssDeclaration& d) { return d.property == property; });
  if (!value.empty()) decls.push_back({property, value});
  std::string css;
  for (const auto& d : decls) css += d.property + ": " + d.value + (d.important ? " !important; " : "; ");
  if (css != node->inlineStyle) {
    node->inlineStyle = std::move(css);
    _dirty = true;
  }
  return true;
}

bool UIDocument::setAttribute(const std::string& id, const std::string& name, const std::string& value) {
  UINode* node = _root->findById(id);
  if (!node) return false;
  if (name == "class" || name == "style" || name == "id") return false;  // use the dedicated setters
  auto& slot = node->attributes[name];
  if (slot != value) {
    slot = value;
    _dirty = true;
  }
  return true;
}

const LayoutBox& UIDocument::layout(glm::vec2 viewport, LayoutMetrics& metrics) {
  if (_dirty || !_layout || viewport != _viewport) {
    _layout = layoutDocument(*_root, *_sheet, viewport, metrics);
    _viewport = viewport;
    _dirty = false;
  }
  return *_layout;
}
