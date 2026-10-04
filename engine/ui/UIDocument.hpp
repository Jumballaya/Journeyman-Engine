#pragma once

#include <memory>
#include <string>

#include "Css.hpp"
#include "Dom.hpp"
#include "Layout.hpp"

// A parsed .ui.html file: the DOM to clone plus its stylesheet (inline
// <style> blocks and linked stylesheets).
struct UITemplate {
  std::shared_ptr<const UINode> root;
  std::shared_ptr<const Stylesheet> sheet;
};

// One live UI screen. Owns a private copy of the template's DOM so scripts
// can mutate it; layout is recomputed lazily after any change.
class UIDocument {
 public:
  explicit UIDocument(const UITemplate& tmpl);

  // Element mutations by id. Each returns false if no element has the id.
  bool setText(const std::string& id, const std::string& text);
  bool setClass(const std::string& id, const std::string& cls, bool on);
  // Sets one inline style property; an empty value removes it.
  bool setStyle(const std::string& id, const std::string& property, const std::string& value);
  bool setAttribute(const std::string& id, const std::string& name, const std::string& value);
  bool has(const std::string& id) { return _root->findById(id) != nullptr; }

  // Re-runs style + layout when the DOM changed or the viewport resized.
  const LayoutBox& layout(glm::vec2 viewport, LayoutMetrics& metrics);
  void invalidate() { _dirty = true; }

 private:
  std::unique_ptr<UINode> _root;
  std::shared_ptr<const Stylesheet> _sheet;
  std::unique_ptr<LayoutBox> _layout;
  glm::vec2 _viewport{0.0f};
  bool _dirty = true;
};
