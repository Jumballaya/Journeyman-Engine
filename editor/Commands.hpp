#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

// Every editor action, once: menus, toolbar buttons, shortcuts and the
// command palette all go through here, so they always agree.
struct Command {
  std::string id;        // "scene.save"
  std::string label;     // "Save Scene"
  std::string category;  // "Scene"; groups the palette
  const char* icon = nullptr;
  ImGuiKeyChord shortcut = 0;
  std::function<void()> run;
  std::function<bool()> enabled;  // null = always
  // Fires even while the running game has the keyboard (play/stop/pause).
  bool whilePlaying = false;
};

class Commands {
 public:
  void add(Command command);
  const Command* find(std::string_view id) const;
  const std::vector<Command>& all() const { return _commands; }
  bool enabled(const Command& command) const { return !command.enabled || command.enabled(); }

  // Runs it if it exists and is enabled; remembers it for the palette.
  bool run(std::string_view id);
  // Most recent first.
  const std::vector<std::string>& recent() const { return _recent; }

  // Once a frame. Plain-key shortcuts wait while a text field is active;
  // only whilePlaying ones fire when the game has the keyboard.
  void handleShortcuts(bool gameHasKeyboard);

  // A menu item showing the command's icon, label and shortcut.
  void menuItem(std::string_view id, bool checked = false);

 private:
  std::vector<Command> _commands;
  std::vector<std::string> _recent;
};

// "⌘S" / "Ctrl+S" for display.
std::string shortcutLabel(ImGuiKeyChord chord);
