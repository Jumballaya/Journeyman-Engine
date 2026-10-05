#pragma once

#include <memory>
#include <string>

class Editor;
class AssetDocument;

// A dedicated editor for one kind of project file, shown as a tab beside the
// Scene and Game views. It draws the whole tab and changes the file only
// through its AssetDocument, so every edit undoes and saves the same way.
class AssetEditor {
 public:
  virtual ~AssetEditor() = default;
  virtual void draw(Editor& editor, AssetDocument& doc) = 0;
  // The Inspector's content while this tab is in use: the properties of what's
  // selected in it. False leaves the Inspector to its usual content.
  virtual bool drawInspector(Editor& editor, AssetDocument& doc) { return false; }
  // A key event (GLFW key, scancode, action) while the tab has focus; true if
  // taken (the input bindings editor listens for the key to bind).
  virtual bool onKey(int key, int scancode, int action) { return false; }
  // The Edit menu's commands ("edit.duplicate", "edit.delete") on what's
  // selected in this tab: whether it takes one now, and doing it.
  virtual bool handles(const std::string& command) const { return false; }
  virtual void run(const std::string& command, AssetDocument& doc) {}
  // While true, the editor's keyboard shortcuts stand aside (a key is being captured).
  virtual bool capturesKeyboard() const { return false; }
};

// The editor for `path`'s kind, or null if it has none (open it as text instead).
std::unique_ptr<AssetEditor> makeAssetEditor(const std::string& path);
