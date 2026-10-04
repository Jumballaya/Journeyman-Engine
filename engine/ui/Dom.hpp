#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// One node of a UI document. Element nodes have a tag; text nodes have an
// empty tag and carry `text`. Attributes keep their raw string values;
// `id`, `class` and `style` are also split out for fast access.
struct UINode {
  std::string tag;  // lowercase; empty for text nodes
  std::string text; // text nodes only (whitespace already collapsed)
  std::string id;
  std::vector<std::string> classes;
  std::string inlineStyle;
  std::unordered_map<std::string, std::string> attributes;

  UINode* parent = nullptr;
  std::vector<std::unique_ptr<UINode>> children;

  bool isText() const { return tag.empty(); }
  bool hasClass(const std::string& c) const;

  UINode& appendChild(std::unique_ptr<UINode> child);
  std::unique_ptr<UINode> clone(UINode* newParent = nullptr) const;

  // Depth-first search for an element with this id (including self).
  UINode* findById(const std::string& id);
};

inline bool UINode::hasClass(const std::string& c) const {
  for (const auto& mine : classes) {
    if (mine == c) return true;
  }
  return false;
}

inline UINode& UINode::appendChild(std::unique_ptr<UINode> child) {
  child->parent = this;
  children.push_back(std::move(child));
  return *children.back();
}

inline std::unique_ptr<UINode> UINode::clone(UINode* newParent) const {
  auto copy = std::make_unique<UINode>();
  copy->tag = tag;
  copy->text = text;
  copy->id = id;
  copy->classes = classes;
  copy->inlineStyle = inlineStyle;
  copy->attributes = attributes;
  copy->parent = newParent;
  for (const auto& child : children) copy->children.push_back(child->clone(copy.get()));
  return copy;
}

inline UINode* UINode::findById(const std::string& wanted) {
  if (!isText() && id == wanted) return this;
  for (auto& child : children) {
    if (UINode* found = child->findById(wanted)) return found;
  }
  return nullptr;
}
