#include "Commands.hpp"

#include <algorithm>

#include "Icons.hpp"

void Commands::add(Command command) { _commands.push_back(std::move(command)); }

const Command* Commands::find(std::string_view id) const {
  auto it = std::find_if(_commands.begin(), _commands.end(), [&](const Command& c) { return c.id == id; });
  return it == _commands.end() ? nullptr : &*it;
}

bool Commands::run(std::string_view id) {
  const Command* command = find(id);
  if (!command || !enabled(*command) || !command->run) return false;
  std::erase(_recent, std::string(id));
  _recent.insert(_recent.begin(), std::string(id));
  if (_recent.size() > 8) _recent.pop_back();
  command->run();
  return true;
}

void Commands::handleShortcuts(bool gameHasKeyboard) {
  const bool typing = ImGui::GetIO().WantTextInput;
  for (const Command& command : _commands) {
    if (!command.shortcut || (gameHasKeyboard && !command.whilePlaying)) continue;
    const bool typesText = (command.shortcut & (ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super)) == 0;
    if (typing && typesText) continue;
    if (ImGui::Shortcut(command.shortcut, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteUnlessBgFocused)) {
      run(command.id);
      return;  // one per frame: a command may change what the others act on
    }
  }
}

void Commands::menuItem(std::string_view id, bool checked) {
  const Command* command = find(id);
  if (!command) return;
  const std::string label = std::string(command->icon ? command->icon : "   ") + "  " + command->label;
  const std::string shortcut = command->shortcut ? shortcutLabel(command->shortcut) : std::string();
  if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked, enabled(*command))) {
    run(id);
  }
}

std::string shortcutLabel(ImGuiKeyChord chord) {
  std::string out;
#ifdef __APPLE__
  if (chord & ImGuiMod_Super) out += ICON_CONTROL;
  if (chord & ImGuiMod_Alt) out += ICON_OPTION;
  if (chord & ImGuiMod_Shift) out += ICON_ARROW_FAT_UP;
  if (chord & ImGuiMod_Ctrl) out += ICON_COMMAND;
#else
  if (chord & ImGuiMod_Ctrl) out += "Ctrl+";
  if (chord & ImGuiMod_Shift) out += "Shift+";
  if (chord & ImGuiMod_Alt) out += "Alt+";
  if (chord & ImGuiMod_Super) out += "Super+";
#endif
  const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
  switch (key) {
    case ImGuiKey_Delete: out += "Del"; break;
    case ImGuiKey_Backspace: out += "Backspace"; break;
    case ImGuiKey_Escape: out += "Esc"; break;
    case ImGuiKey_Enter: out += "Enter"; break;
    case ImGuiKey_Space: out += "Space"; break;
    case ImGuiKey_Comma: out += ","; break;
    case ImGuiKey_Period: out += "."; break;
    case ImGuiKey_Equal: out += "="; break;
    case ImGuiKey_Minus: out += "-"; break;
    case ImGuiKey_Slash: out += "/"; break;
    case ImGuiKey_Apostrophe: out += "'"; break;
    default: out += ImGui::GetKeyName(key);
  }
  return out;
}
