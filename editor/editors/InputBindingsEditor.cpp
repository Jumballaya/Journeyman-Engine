// The input actions editor (*.bindings.json): each action with its keys and
// gamepad controls as chips. Bind by pressing the key or button; see which
// scripts use each action, and add the ones scripts use but nobody bound.

#include <algorithm>
#include <map>
#include <regex>
#include <set>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "Icons.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "core/events/EventBus.hpp"
#include "inputs/InputActions.hpp"
#include "inputs/InputsModule.hpp"

namespace {

constexpr const char* kPadControls[] = {
    "A", "B", "X", "Y", "LeftBumper", "RightBumper", "LeftTrigger", "RightTrigger", "Back", "Start", "Guide",
    "LeftThumb", "RightThumb", "DPadUp", "DPadDown", "DPadLeft", "DPadRight", "LeftStickUp", "LeftStickDown",
    "LeftStickLeft", "LeftStickRight", "RightStickUp", "RightStickDown", "RightStickLeft", "RightStickRight"};

// The engine's name for a key event, by physical position like the game reads it.
std::string keyControl(int key, int scancode) {
  static InputsManager keys = [] {
    InputsManager m;
    EventBus unused(1);
    m.initialize(unused);
    return m;
  }();
  return std::string(inputs::keyName(keys.keyFromEvent(scancode, key)));
}

// "ArrowLeft" → "←", "Gamepad.LeftStickLeft" → "L Stick ←": how a binding reads on its chip.
std::string controlLabel(const std::string& control) {
  static const std::map<std::string, std::string> kNames = {
      {"ArrowLeft", ICON_ARROW_LEFT}, {"ArrowRight", ICON_ARROW_RIGHT}, {"ArrowUp", ICON_ARROW_UP},
      {"ArrowDown", ICON_ARROW_DOWN}, {"Escape", "Esc"}, {"Enter", ICON_KEY_RETURN "  Enter"},
      {"Backspace", "Backspace"}, {"Minus", "-"}, {"Equal", "="}, {"Backtick", "`"}, {"LeftBracket", "["},
      {"RightBracket", "]"}, {"Backslash", "\\"}, {"Semicolon", ";"}, {"Apostrophe", "'"}, {"Comma", ","},
      {"Period", "."}, {"Slash", "/"}, {"PageUp", "Page Up"}, {"PageDown", "Page Down"},
      {"LeftShift", "Left Shift"}, {"RightShift", "Right Shift"}, {"LeftCtrl", "Left Ctrl"},
      {"RightCtrl", "Right Ctrl"}, {"LeftAlt", "Left Alt"}, {"RightAlt", "Right Alt"},
      {"LeftSuper", "Left Super"}, {"RightSuper", "Right Super"}, {"KPEnter", "Num Enter"},
      {"KPAdd", "Num +"}, {"KPSubtract", "Num -"}, {"KPMultiply", "Num *"}, {"KPDivide", "Num /"},
      {"KPPeriod", "Num ."}, {"CapsLock", "Caps Lock"},
      {"MouseLeft", ICON_MOUSE_LEFT_CLICK "  Left Click"}, {"MouseRight", ICON_MOUSE_RIGHT_CLICK "  Right Click"},
      {"MouseMiddle", ICON_MOUSE_MIDDLE_CLICK "  Middle Click"},
      // Gamepad, without the prefix.
      {"LeftBumper", "LB"}, {"RightBumper", "RB"}, {"LeftTrigger", "LT"}, {"RightTrigger", "RT"},
      {"LeftThumb", "L3"}, {"RightThumb", "R3"}, {"DPadUp", "D-Pad " ICON_ARROW_UP},
      {"DPadDown", "D-Pad " ICON_ARROW_DOWN}, {"DPadLeft", "D-Pad " ICON_ARROW_LEFT},
      {"DPadRight", "D-Pad " ICON_ARROW_RIGHT}, {"LeftStickUp", "L Stick " ICON_ARROW_UP},
      {"LeftStickDown", "L Stick " ICON_ARROW_DOWN}, {"LeftStickLeft", "L Stick " ICON_ARROW_LEFT},
      {"LeftStickRight", "L Stick " ICON_ARROW_RIGHT}, {"RightStickUp", "R Stick " ICON_ARROW_UP},
      {"RightStickDown", "R Stick " ICON_ARROW_DOWN}, {"RightStickLeft", "R Stick " ICON_ARROW_LEFT},
      {"RightStickRight", "R Stick " ICON_ARROW_RIGHT}};
  const bool pad = control.starts_with("Gamepad.");
  const std::string name = pad ? control.substr(8) : control;
  std::string label = name;
  if (auto it = kNames.find(name); it != kNames.end()) label = it->second;
  else if (name.starts_with("Digit")) label = name.substr(5);
  else if (name.starts_with("KP")) label = "Num " + name.substr(2);
  return pad ? std::string(ICON_GAME_CONTROLLER "  ") + label : label;
}

// Every gamepad control's value now (0..1), merged over connected pads.
std::map<std::string, float> padValues() {
  std::map<std::string, float> out;
  for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
    GLFWgamepadstate s;
    if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &s)) continue;
    auto set = [&](const char* name, float v) { out[name] = std::max(out[name], v); };
    static constexpr const char* kButtons[] = {"A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start",
                                               "Guide", "LeftThumb", "RightThumb", "DPadUp", "DPadRight", "DPadDown",
                                               "DPadLeft"};
    for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) set(kButtons[b], s.buttons[b] == GLFW_PRESS ? 1.0f : 0.0f);
    auto stick = [&](float v, const char* neg, const char* pos) {
      set(neg, std::max(0.0f, -v));
      set(pos, std::max(0.0f, v));
    };
    stick(s.axes[GLFW_GAMEPAD_AXIS_LEFT_X], "LeftStickLeft", "LeftStickRight");
    stick(s.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], "LeftStickUp", "LeftStickDown");
    stick(s.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], "RightStickLeft", "RightStickRight");
    stick(s.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], "RightStickUp", "RightStickDown");
    set("LeftTrigger", (s.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1) * 0.5f);
    set("RightTrigger", (s.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1) * 0.5f);
  }
  return out;
}

// Which scripts read which actions (string literals in Input.*(...) calls).
class ActionUsage {
 public:
  const std::map<std::string, std::set<std::string>>& of(const Project& project) {
    // Recomputed when any script changes.
    size_t signature = 0;
    for (const AssetFile& f : project.files()) {
      if (f.kind == AssetKind::Script) signature = signature * 31 + std::hash<std::string>()(f.path) + f.modified.time_since_epoch().count();
    }
    if (signature == _signature) return _usage;
    _signature = signature;
    _usage.clear();
    static const std::regex call(R"(Input\s*\.\s*\w+\s*\(([^)]*)\))");
    static const std::regex literal(R"re("([A-Za-z0-9_.\-]+)")re");
    for (const AssetFile& f : project.files()) {
      if (f.kind != AssetKind::Script || f.path.find("node_modules") != std::string::npos) continue;
      const std::string text = project.readText(f.path);
      const std::string name = std::filesystem::path(f.path).filename().string();
      for (auto c = std::sregex_iterator(text.begin(), text.end(), call); c != std::sregex_iterator(); ++c) {
        const std::string args = (*c)[1];
        for (auto l = std::sregex_iterator(args.begin(), args.end(), literal); l != std::sregex_iterator(); ++l) {
          _usage[(*l)[1]].insert(name);
        }
      }
    }
    return _usage;
  }

 private:
  size_t _signature = 0;
  std::map<std::string, std::set<std::string>> _usage;
};

std::string joined(const std::set<std::string>& names) {
  std::string out;
  for (const std::string& s : names) out += (out.empty() ? "" : ", ") + s;
  return out;
}

class InputBindingsEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool onKey(int key, int scancode, int action) override;
  bool capturesKeyboard() const override { return !_listening.empty(); }

 private:
  std::string _filter;
  std::string _listening;  // the action waiting for a key or button, or ""
  bool _overListenChip = false;  // last frame the pointer was on the "press a key" chip
  std::map<std::string, float> _padBaseline;  // pad values when listening began
  std::string _renaming, _renameText;
  bool _renameFocus = false;
  std::string _pendingKey;  // set by onKey, applied in draw (where the document is)
  ActionUsage _usage;

  void listen(const std::string& action) { _listening = action, _padBaseline = padValues(); }
  void startRename(const std::string& action) { _renaming = action, _renameText = action, _renameFocus = true; }
  void updateListening(AssetDocument& doc);
  void bind(AssetDocument& doc, const std::string& action, const std::string& control);
  void drawName(AssetDocument& doc, const std::string& action);
  void drawControls(AssetDocument& doc, const std::string& action, const Json& controls, bool pad, InputsModule* live);
  void drawAction(AssetDocument& doc, const std::string& action, const Json& controls, const std::string& readers,
                  InputsModule* live);
};

bool InputBindingsEditor::onKey(int key, int scancode, int action) {
  if (_listening.empty() || action != GLFW_PRESS) return false;
  const std::string name = keyControl(key, scancode);
  if (!name.empty()) _pendingKey = name;
  return true;
}

void InputBindingsEditor::bind(AssetDocument& doc, const std::string& action, const std::string& control) {
  doc.edit("Bind " + controlLabel(control) + " to " + action, [&](Json& v) {
    Json& list = v["actions"][action];
    if (!list.is_array()) list = Json::array();
    if (std::find(list.begin(), list.end(), control) == list.end()) list.push_back(control);
  });
}

// Binds what was pressed since last frame (key, mouse button on the chip, pad
// control), or cancels on a click elsewhere.
void InputBindingsEditor::updateListening(AssetDocument& doc) {
  const bool overChip = std::exchange(_overListenChip, false);  // drawing the chip sets it again
  std::string control = std::exchange(_pendingKey, "");
  if (_listening.empty()) return;
  for (const auto& [name, value] : padValues()) {
    if (control.empty() && value > 0.6f && _padBaseline[name] < 0.3f) control = "Gamepad." + name;
  }
  if (!control.empty()) bind(doc, _listening, control);
  if (!control.empty() || (!overChip && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))) {
    _listening.clear();
  }
}

// Double-click to rename.
void InputBindingsEditor::drawName(AssetDocument& doc, const std::string& action) {
  ImGui::AlignTextToFramePadding();
  if (_renaming != action) {
    ImGui::PushFont(theme::fonts().medium, theme::sizeBody);
    ImGui::TextUnformatted(action.c_str());
    ImGui::PopFont();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) startRename(action);
    return;
  }
  if (std::exchange(_renameFocus, false)) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-1);
  const bool done = ImGui::InputText("##rename", &_renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  if (!done && !ImGui::IsItemDeactivated()) return;
  const std::string to = _renameText;
  if (!to.empty() && to != action && !doc.value()["actions"].contains(to)) {
    doc.edit("Rename Action " + action, [&](Json& v) {
      Json renamed = Json::object();
      for (auto& [k, c] : v["actions"].items()) renamed[k == action ? to : k] = c;
      v["actions"] = renamed;
    });
  }
  _renaming.clear();
}

void InputBindingsEditor::drawControls(AssetDocument& doc, const std::string& action, const Json& controls, bool pad, InputsModule* live) {
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
  for (size_t i = 0; i < controls.size(); ++i) {
    if (!controls[i].is_string()) continue;
    const std::string control = controls[i];
    if (control.starts_with("Gamepad.") != pad) continue;
    ImGui::PushID(static_cast<int>(i));
    // While the game runs, a held key lights up.
    bool held = false;
    if (live && !pad) {
      if (auto parsed = inputs::parseControl(control); parsed && std::holds_alternative<inputs::Key>(*parsed)) {
        held = live->getManager().keyIsDown(std::get<inputs::Key>(*parsed));
      }
    }
    bool removed = false;
    if (held) ImGui::PushStyleColor(ImGuiCol_Text, theme::accentBright);
    ui::chip("c", controlLabel(control).c_str(), true, &removed, !pad);
    if (held) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !removed) ui::tooltip(control.c_str());
    if (removed) {
      doc.edit("Unbind " + controlLabel(control) + " from " + action, [&](Json& v) {
        Json& list = v["actions"][action];
        if (auto it = std::find(list.begin(), list.end(), control); it != list.end()) list.erase(it);
      });
    }
    ImGui::PopID();
    ImGui::SameLine(0, 4);
  }
}

void InputBindingsEditor::drawAction(AssetDocument& doc, const std::string& action, const Json& controls,
                                     const std::string& readers, InputsModule* live) {
  ImGui::PushID(action.c_str());
  ImGui::TableNextRow(0, 40);
  if (live && live->getActions().value(action, live->getManager()) > 0.0f) {
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, theme::u32(theme::accent, 0.18f));  // the game sees it now
  }
  ImGui::TableNextColumn();
  drawName(doc, action);

  // Keyboard, then gamepad, each ending in an add button.
  const float addH = ImGui::GetFrameHeight() - 2;
  ImGui::TableNextColumn();
  drawControls(doc, action, controls, false, live);
  if (_listening == action) {
    // Pulsing "press a key" chip; a mouse button clicked on it binds that button.
    const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::withAlpha(theme::accentBright, pulse));
    ui::chip("listen", ICON_RECORD "  Press a key, or click here", false, nullptr, false);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      _overListenChip = true;
      static constexpr const char* kButtons[] = {"MouseLeft", "MouseRight", "MouseMiddle"};
      for (int b = 0; b < 3; ++b) {
        if (ImGui::IsMouseClicked(b)) _pendingKey = kButtons[b];
      }
    }
    ui::tooltip("Press the key or gamepad button to bind, or click this with the mouse button to bind. Click anywhere else to cancel.");
  } else if (ui::iconButton("addKey", ICON_PLUS, "Bind a key (or gamepad button): click, then press it", false, 0, addH)) {
    listen(action);
  }
  ImGui::TableNextColumn();
  drawControls(doc, action, controls, true, live);
  if (ui::iconButton("addPad", ICON_PLUS, "Bind a gamepad control", false, 0, addH)) ImGui::OpenPopup("pad");
  if (ImGui::BeginPopup("pad")) {
    ui::sectionLabel("Gamepad", 200);
    for (const char* name : kPadControls) {
      const std::string control = std::string("Gamepad.") + name;
      if (ImGui::MenuItem(controlLabel(control).c_str())) bind(doc, action, control);
    }
    ImGui::EndPopup();
  }

  // Who reads it.
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  if (readers.empty()) ImGui::TextColored(theme::textFaint, "No script reads it");
  else ImGui::TextColored(theme::textDim, ICON_CODE "  %s", readers.c_str());
  ImGui::PopFont();
  if (!readers.empty() && ImGui::IsItemHovered()) ui::tooltip(("Read by " + readers).c_str());
  if (controls.empty()) {
    ImGui::SameLine(0, 8);
    ui::badge("Unbound", theme::warning);
  }

  // Row menu.
  ImGui::TableNextColumn();
  if (ui::iconButton("menu", ICON_DOTS_THREE, "More")) ImGui::OpenPopup("row");
  if (ImGui::BeginPopup("row")) {
    if (ImGui::MenuItem(ICON_PENCIL_SIMPLE "  Rename")) startRename(action);
    if (ImGui::MenuItem(ICON_COPY "  Duplicate")) {
      doc.edit("Duplicate Action " + action, [&](Json& v) {
        std::string name = action + "_copy";
        for (int n = 2; v["actions"].contains(name); ++n) name = action + "_copy" + std::to_string(n);
        v["actions"][name] = controls;
      });
    }
    if (ImGui::MenuItem(ICON_X "  Clear Bindings", nullptr, false, !controls.empty())) {
      doc.edit("Clear " + action, [&](Json& v) { v["actions"][action] = Json::array(); });
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_TRASH "  Delete")) doc.edit("Delete Action " + action, [&](Json& v) { v["actions"].erase(action); });
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

void InputBindingsEditor::draw(Editor& editor, AssetDocument& doc) {
  updateListening(doc);
  const Json actions = doc.value().value("actions", Json::object());
  const auto& usage = _usage.of(*editor.project());

  const float right = ui::beginDocumentBar(ICON_GAME_CONTROLLER, "Input Actions", doc.path().c_str());
  const float search = 220.0f;
  ImGui::SameLine(right - search - 128);
  ui::searchField("filter", _filter, "Filter actions", search);
  ImGui::SameLine(0, 8);
  if (ui::primaryButton(ICON_PLUS "  Add Action", {120, 0})) {
    std::string name = "action";
    for (int n = 2; actions.contains(name); ++n) name = "action" + std::to_string(n);
    doc.edit("Add Action", [&](Json& v) { v["actions"][name] = Json::array(); });
    _filter.clear();  // the new row, with its name field, must be visible
    startRename(name);
  }
  ui::endDocumentBar();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 4});
  ImGui::BeginChild("##actions", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  InputsModule* live = editor.game() ? editor.game()->engine().getModules().find<InputsModule>() : nullptr;
  if (editor.game()) {
    ImGui::Dummy({0, 4});
    ui::badge(ICON_PLAY "  Live", theme::accent);
    ImGui::SameLine(0, 8);
    ui::smallText("Actions light up as the running game sees them. Edits here apply from the next Play.", theme::textDim);
    ImGui::Dummy({0, 2});
  }
  if (actions.empty()) {
    ui::emptyState(ICON_GAME_CONTROLLER, "No actions yet",
                   "Actions name what the player does (\"jump\", \"fire\"). Scripts ask Input.pressed(\"jump\"); "
                   "the keys and buttons for it live here.");
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {8, 6});
    if (ImGui::BeginTable("##table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 150);
      ImGui::TableSetupColumn(ICON_KEYBOARD "  Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.3f);
      ImGui::TableSetupColumn(ICON_GAME_CONTROLLER "  Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.3f);
      ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthStretch, 0.8f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
      ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
      ImGui::TableHeadersRow();
      ImGui::PopStyleColor();
      ImGui::PopFont();
      for (const auto& [action, controls] : actions.items()) {
        if (!_filter.empty() && ui::fuzzyScore(action, _filter) < 0) continue;
        auto users = usage.find(action);
        drawAction(doc, action, controls.is_array() ? controls : Json::array(), users == usage.end() ? "" : joined(users->second), live);
      }
      ImGui::EndTable();
    }
    ImGui::PopStyleVar();
  }

  // Actions scripts read that nothing binds: one click defines each.
  bool first = true;
  for (const auto& [name, users] : usage) {
    if (actions.contains(name) || name.find('.') != std::string::npos) continue;
    if (std::exchange(first, false)) {
      ImGui::Dummy({0, 10});
      ui::sectionLabel("Read by scripts but not defined");
      ImGui::Dummy({0, 2});
    }
    ImGui::PushID(name.c_str());
    if (ui::chip("missing", (std::string(ICON_PLUS "  ") + name).c_str())) {
      doc.edit("Add Action " + name, [&](Json& v) { v["actions"][name] = Json::array(); });
      _filter.clear();
      listen(name);
    }
    ui::tooltip(("Read by " + joined(users) + ". Click to add it, then press its key.").c_str());
    ImGui::PopID();
    ImGui::SameLine(0, 6);
  }
  if (!first) ImGui::NewLine();
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

}  // namespace

std::unique_ptr<AssetEditor> makeInputBindingsEditor() { return std::make_unique<InputBindingsEditor>(); }
