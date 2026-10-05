#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// A UI document node: elements have a tag, text nodes an empty tag and `text`.
// id, class and style are also split out of the attributes.
struct UINode {
  std::string tag;  // lowercase; empty for text nodes
  std::string text; // text nodes only (whitespace already collapsed)
  std::string id;
  std::vector<std::string> classes;
  std::string inlineStyle;
  std::unordered_map<std::string, std::string> attributes;

  // Where the node came from in its HTML (byte offsets), for editors that
  // write changes back: [start, openEnd) is the open tag (or the text),
  // [openEnd, closeStart) the content, and end is past the close tag.
  struct Source {
    size_t start = 0, openEnd = 0, closeStart = 0, end = 0;
  };
  Source source;

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
  copy->source = source;
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
