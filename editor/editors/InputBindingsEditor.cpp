// The input actions editor (*.bindings.json): each action with its keys and
// gamepad controls as chips. Bind by pressing the key or button; see which
// scripts use each action, and add the ones scripts use but nobody bound.

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

  void bind(AssetDocument& doc, const std::string& action, const std::string& control);
  void drawAction(Editor& editor, AssetDocument& doc, const std::string& action, const Json& controls,
                  const std::set<std::string>* users);
  void drawControls(AssetDocument& doc, const std::string& action, const Json& controls, bool pad, InputsModule* live);
  void listenToPads(AssetDocument& doc);
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
    for (const Json& c : list) {
      if (c == control) return;
    }
    list.push_back(control);
  });
}

void InputBindingsEditor::listenToPads(AssetDocument& doc) {
  if (_listening.empty()) return;
  for (const auto& [control, value] : padValues()) {
    const float before = _padBaseline.contains(control) ? _padBaseline[control] : 0.0f;
    if (value > 0.6f && before < 0.3f) {
      bind(doc, _listening, "Gamepad." + control);
      _listening.clear();
      return;
    }
  }
}

void InputBindingsEditor::drawControls(AssetDocument& doc, const std::string& action, const Json& controls, bool pad, InputsModule* live) {
  for (size_t i = 0; i < controls.size(); ++i) {
    if (!controls[i].is_string()) continue;
    const std::string control = controls[i];
    if (control.starts_with("Gamepad.") != pad) continue;
    ImGui::PushID(static_cast<int>(i));
    bool removed = false;
    // While the game runs, a held key lights up.
    bool held = false;
    if (live && !pad) {
      if (auto parsed = inputs::parseControl(control); parsed && std::holds_alternative<inputs::Key>(*parsed)) {
        held = live->getManager().keyIsDown(std::get<inputs::Key>(*parsed));
      }
    }
    if (held) ImGui::PushStyleColor(ImGuiCol_Text, theme::accentBright);
    ui::chip("c", controlLabel(control).c_str(), true, &removed, !pad);
    if (held) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !removed) ui::tooltip(control.c_str());
    if (removed) {
      doc.edit("Unbind " + controlLabel(control) + " from " + action, [&](Json& v) {
        Json& list = v["actions"][action];
        for (size_t k = 0; k < list.size(); ++k) {
          if (list[k] == control) {
            list.erase(k);
            break;
          }
        }
      });
    }
    ImGui::PopID();
    ImGui::SameLine(0, 4);
  }
}

void InputBindingsEditor::drawAction(Editor& editor, AssetDocument& doc, const std::string& action, const Json& controls,
                                     const std::set<std::string>* users) {
  ImGui::PushID(action.c_str());
  ImGui::TableNextRow(0, 40);
  InputsModule* live = editor.game() ? editor.game()->engine().getModules().find<InputsModule>() : nullptr;
  if (live && live->getActions().value(action, live->getManager()) > 0.0f) {
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, theme::u32(theme::accent, 0.18f));  // the game sees it now
  }

  // Name (double-click to rename).
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  if (_renaming == action) {
    if (_renameFocus) ImGui::SetKeyboardFocusHere(), _renameFocus = false;
    ImGui::SetNextItemWidth(-1);
    const bool done = ImGui::InputText("##rename", &_renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (done || ImGui::IsItemDeactivated()) {
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
  } else {
    ImGui::PushFont(theme::fonts().medium, theme::sizeBody);
    ImGui::TextUnformatted(action.c_str());
    ImGui::PopFont();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      _renaming = action, _renameText = action, _renameFocus = true;
    }
  }

  // Keyboard, then gamepad, each ending in an add button.
  for (int column = 0; column < 2; ++column) {
    const bool pad = column == 1;
    ImGui::TableNextColumn();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
    drawControls(doc, action, controls, pad, live);
    const bool listeningHere = _listening == action;
    if (!pad && listeningHere) {
      // Pulsing "press a key" chip; any click elsewhere cancels.
      const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::withAlpha(theme::accentBright, pulse));
      ui::chip("listen", ICON_RECORD "  Press a key, or click here", false, nullptr, false);
      ImGui::PopStyleColor();
      // Clicked with a mouse button: that button binds (a click anywhere else cancels).
      if (ImGui::IsItemHovered()) {
        _overListenChip = true;
        static constexpr const char* kButtons[] = {"MouseLeft", "MouseRight", "MouseMiddle"};
        for (int b = 0; b < 3; ++b) {
          if (ImGui::IsMouseClicked(b)) _pendingKey = kButtons[b];
        }
      }
      ui::tooltip("Press the key or gamepad button to bind, or click this with the mouse button to bind. Click anywhere else to cancel.");
    } else if (!pad) {
      if (ui::iconButton("addKey", ICON_PLUS, "Bind a key (or gamepad button): click, then press it", false, 0,
                         ImGui::GetFrameHeight() - 2)) {
        _listening = action;
        _padBaseline = padValues();
      }
    } else {
      if (ui::iconButton("addPad", ICON_PLUS, "Bind a gamepad control", false, 0, ImGui::GetFrameHeight() - 2)) {
        ImGui::OpenPopup("pad");
      }
      if (ImGui::BeginPopup("pad")) {
        ui::sectionLabel("Gamepad", 200);
        for (const char* control : kPadControls) {
          if (ImGui::MenuItem(controlLabel(std::string("Gamepad.") + control).c_str())) bind(doc, action, std::string("Gamepad.") + control);
        }
        ImGui::EndPopup();
      }
    }
  }

  // Who reads it.
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  if (users && !users->empty()) {
    std::string list;
    for (const std::string& s : *users) list += (list.empty() ? "" : ", ") + s;
    ImGui::PushFont(nullptr, theme::sizeSmall);
    ImGui::TextColored(theme::textDim, ICON_CODE "  %s", list.c_str());
    ImGui::PopFont();
    if (ImGui::IsItemHovered()) ui::tooltip(("Read by " + list).c_str());
  } else {
    ImGui::PushFont(nullptr, theme::sizeSmall);
    ImGui::TextColored(theme::textFaint, "No script reads it");
    ImGui::PopFont();
  }
  if (controls.empty()) {
    ImGui::SameLine(0, 8);
    ui::badge("Unbound", theme::warning);
  }

  // Row menu.
  ImGui::TableNextColumn();
  if (ui::iconButton("menu", ICON_DOTS_THREE, "More")) ImGui::OpenPopup("row");
  if (ImGui::BeginPopup("row")) {
    if (ImGui::MenuItem(ICON_PENCIL_SIMPLE "  Rename")) _renaming = action, _renameText = action, _renameFocus = true;
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
  (void)editor;
  ImGui::PopID();
}

void InputBindingsEditor::draw(Editor& editor, AssetDocument& doc) {
  if (!_pendingKey.empty()) {
    if (!_listening.empty()) bind(doc, _listening, _pendingKey);
    _listening.clear();
    _pendingKey.clear();
  }
  listenToPads(doc);
  if (!_listening.empty() && !_overListenChip && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
    _listening.clear();
  }
  _overListenChip = false;  // drawing the chip sets it again

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
    _renaming = name, _renameText = name, _renameFocus = true;
  }
  ui::endDocumentBar();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 4});
  ImGui::BeginChild("##actions", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
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
        drawAction(editor, doc, action, controls.is_array() ? controls : Json::array(), users == usage.end() ? nullptr : &users->second);
      }
      ImGui::EndTable();
    }
    ImGui::PopStyleVar();
  }

  // Actions scripts read that nothing binds: one click defines each.
  std::vector<std::string> missing;
  for (const auto& [name, users] : usage) {
    if (!actions.contains(name) && name.find('.') == std::string::npos) missing.push_back(name);
  }
  if (!missing.empty()) {
    ImGui::Dummy({0, 10});
    ui::sectionLabel("Read by scripts but not defined");
    ImGui::Dummy({0, 2});
    for (const std::string& name : missing) {
      ImGui::PushID(name.c_str());
      if (ui::chip("missing", (std::string(ICON_PLUS "  ") + name).c_str())) {
        doc.edit("Add Action " + name, [&](Json& v) { v["actions"][name] = Json::array(); });
        _listening = name;
        _padBaseline = padValues();
      }
      std::string users;
      for (const std::string& s : usage.at(name)) users += (users.empty() ? "" : ", ") + s;
      ui::tooltip(("Read by " + users + ". Click to add it, then press its key.").c_str());
      ImGui::PopID();
      ImGui::SameLine(0, 6);
    }
    ImGui::NewLine();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

}  // namespace

std::unique_ptr<AssetEditor> makeInputBindingsEditor() { return std::make_unique<InputBindingsEditor>(); }
