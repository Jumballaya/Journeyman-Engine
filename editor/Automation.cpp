#include "Automation.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "Editor.hpp"
#include "LogBook.hpp"
#include "panels/Panels.hpp"
#include "stb_image_write.h"

void savePng(const std::string& path, int width, int height) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  const size_t row = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height / 2; ++y) {  // GL rows are bottom-up
    std::swap_ranges(pixels.begin() + y * row, pixels.begin() + (y + 1) * row, pixels.begin() + (height - 1 - y) * row);
  }
  stbi_write_png(path.c_str(), width, height, 4, pixels.data(), static_cast<int>(row));
}

namespace {

ImGuiKey imguiKey(const std::string& name) {
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
    if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(k))) return static_cast<ImGuiKey>(k);
  }
  return ImGuiKey_None;
}

int glfwKey(const std::string& name) {
  const unsigned char c = name.empty() ? 0 : static_cast<unsigned char>(name[0]);
  if (name.size() == 1 && std::isalpha(c)) return GLFW_KEY_A + (std::toupper(c) - 'A');
  if (name.size() == 1 && std::isdigit(c)) return GLFW_KEY_0 + (c - '0');
  static const std::map<std::string, int> kNamed = {
      {"Space", GLFW_KEY_SPACE}, {"Enter", GLFW_KEY_ENTER}, {"Escape", GLFW_KEY_ESCAPE}, {"Tab", GLFW_KEY_TAB},
      {"Backspace", GLFW_KEY_BACKSPACE}, {"Left", GLFW_KEY_LEFT}, {"Right", GLFW_KEY_RIGHT}, {"Up", GLFW_KEY_UP},
      {"Down", GLFW_KEY_DOWN}, {"LeftShift", GLFW_KEY_LEFT_SHIFT}, {"F1", GLFW_KEY_F1}, {"F5", GLFW_KEY_F5}};
  auto it = kNamed.find(name);
  return it == kNamed.end() ? GLFW_KEY_UNKNOWN : it->second;
}

// Simulated input for automation: "@mouse 400 300", "@down", "@key W", "@select Player".
void simulate(Editor& editor, const std::string& step) {
  static const std::map<std::string, std::pair<ImGuiMouseButton, bool>> kButtons = {
      {"@down", {ImGuiMouseButton_Left, true}},    {"@up", {ImGuiMouseButton_Left, false}},
      {"@rdown", {ImGuiMouseButton_Right, true}},  {"@rup", {ImGuiMouseButton_Right, false}},
      {"@mdown", {ImGuiMouseButton_Middle, true}}, {"@mup", {ImGuiMouseButton_Middle, false}}};
  ImGuiIO& io = ImGui::GetIO();
  std::istringstream in(step);
  std::string verb, rest, word;
  in >> verb;
  std::getline(in >> std::ws, rest);  // the argument: paths and names may have spaces
  std::istringstream args(rest);
  std::istringstream(rest) >> word;

  if (verb == "@mouse") {
    float x = 0, y = 0;
    args >> x >> y;
    io.AddMousePosEvent(x, y);
  } else if (auto button = kButtons.find(verb); button != kButtons.end()) {
    io.AddMouseButtonEvent(button->second.first, button->second.second);
  } else if (verb == "@wheel" || verb == "@scroll") {
    float dx = 0, dy = 0;
    if (verb == "@scroll") args >> dx;  // a trackpad: fractional and sideways deltas
    args >> dy;
    io.AddMouseWheelEvent(dx, dy);
  } else if (verb == "@hold" || verb == "@unhold" || verb == "@key") {
    // @hold keeps a key down across steps (Space to pan, Shift while clicking); @key taps it.
    const ImGuiKey key = imguiKey(word);
    if (key == ImGuiKey_None) return;
    io.AddKeyEvent(key, verb != "@unhold");
    if (verb == "@key") io.AddKeyEvent(key, false);
  } else if (verb == "@ctrl" || verb == "@shift" || verb == "@super" || verb == "@release") {
    // A modifier held until @release (@super is Cmd on a Mac: select all, copy, paste).
    const bool release = verb == "@release";
    if (release || verb == "@ctrl") io.AddKeyEvent(ImGuiMod_Ctrl, !release);
    if (release || verb == "@shift") io.AddKeyEvent(ImGuiMod_Shift, !release);
    if (release || verb == "@super") io.AddKeyEvent(ImGuiMod_Super, !release);
  } else if (verb == "@press" || verb == "@keydown" || verb == "@keyup") {
    // A physical key, as the window would deliver it (what "press a key to bind" listens
    // for, and what the game reads); @keydown holds it until @keyup, as a player would.
    const int key = glfwKey(word);
    if (key == GLFW_KEY_UNKNOWN) return;
    if (verb != "@keyup") editor.onKey(key, glfwGetKeyScancode(key), GLFW_PRESS);
    if (verb != "@keydown") editor.onKey(key, glfwGetKeyScancode(key), GLFW_RELEASE);
  } else if (verb == "@type") {
    io.AddInputCharactersUTF8(rest.c_str());
  } else if (verb == "@import") {
    editor.importFiles({std::filesystem::path(rest)}, editor.assetsFolder());
  } else if (verb == "@add") {
    editor.instantiateAsset(rest, editor.scenePanel().viewCenter());
  } else if (verb == "@apply") {
    editor.applyAssetToEntity(editor.primary(), rest);
  } else if (verb == "@live") {
    if (HostedEngine* game = editor.game()) {
      auto found = game->engine().getWorld().findWithTag(word);
      if (!found.empty()) editor.selectLive(*found.begin());
    }
  } else if (verb == "@makeprefab") {
    editor.createPrefab(editor.primary(), editor.assetsFolderForPrefabs());
  } else if (verb == "@move") {
    std::string from, to;
    args >> from >> to;
    editor.moveAsset(from, to);
  } else if (verb == "@reveal") {
    editor.revealAsset(rest);
  } else if (verb == "@open") {
    editor.openAsset(rest);
  } else if (verb == "@inspect") {
    editor.inspectAsset(rest);
  } else if (verb == "@select") {
    if (SceneDocument* scene = editor.scene()) {
      for (size_t i = 0; i < scene->size(); ++i) {
        if (scene->displayName(i) == rest) editor.select(scene->uid(i));
      }
    }
  }
}

}  // namespace

Automation::Automation(const std::string& script, std::filesystem::path control) : _control(std::move(control)) {
  std::stringstream in(script);
  for (std::string item; std::getline(in, item, ';');) {
    const size_t colon = item.find(':');
    if (colon != std::string::npos) _timed.emplace(std::atoi(item.substr(0, colon).c_str()), item.substr(colon + 1));
  }
  if (live()) {
    std::error_code ec;
    std::filesystem::create_directories(_control, ec);
    std::ofstream(_control / "in", std::ios::trunc);  // a fresh session
    std::ofstream(_control / "done", std::ios::trunc);
  }
}

bool Automation::expand(const std::string& step) {
  std::istringstream in(step);
  std::string verb;
  float x = 0, y = 0, x2 = 0, y2 = 0;
  if (!(in >> verb >> x >> y)) return false;
  auto at = [](float px, float py) { return "@mouse " + std::to_string(px) + " " + std::to_string(py); };
  // The button: left, or r(ight) / m(iddle) as the verb's first letter says.
  const std::string b = verb[1] == 'r' || verb[1] == 'm' ? verb.substr(1, 1) : "";
  if (verb == "@click" || verb == "@rclick" || verb == "@dblclick") {
    _queue.push_back(at(x, y));
    for (int i = verb == "@dblclick" ? 2 : 1; i > 0; --i) {
      _queue.push_back("@" + b + "down");
      _queue.push_back("@" + b + "up");
    }
    return true;
  }
  if ((verb != "@drag" && verb != "@rdrag" && verb != "@mdrag") || !(in >> x2 >> y2)) return false;
  // Down, a few moves on the way (drag thresholds, drop targets), up.
  _queue.push_back(at(x, y));
  _queue.push_back("@" + b + "down");
  for (int i = 1; i <= 8; ++i) _queue.push_back(at(x + (x2 - x) * i / 8.0f, y + (y2 - y) * i / 8.0f));
  _queue.push_back(at(x2, y2));
  _queue.push_back("@" + b + "up");
  return true;
}

void Automation::readControl() {
  std::ifstream in(_control / "in", std::ios::binary);
  if (!in) return;
  in.seekg(static_cast<std::streamoff>(_read));
  std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  _read += chunk.size();
  _partial += chunk;
  for (size_t nl = _partial.find('\n'); nl != std::string::npos; nl = _partial.find('\n')) {
    std::string line = _partial.substr(0, nl);
    _partial.erase(0, nl + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] != '#' && !expand(line)) _queue.push_back(line);
  }
}

void Automation::run(Editor& editor, const std::string& step) {
  if (step.starts_with("@wait")) {
    _wait = std::max(0, std::atoi(step.substr(5).c_str()));
  } else if (step.starts_with("@shot ")) {
    _shot = step.substr(6);
  } else if (step.starts_with("@done ")) {
    if (live()) std::ofstream(_control / "done", std::ios::trunc) << step.substr(6);
  } else if (step.starts_with("@")) {
    simulate(editor, step);
  } else if (!editor.commands().run(step)) {
    LogBook::instance().add(LogBook::Level::Warning, LogBook::Source::Editor, "automation: '" + step + "' didn't run");
  }
}

void Automation::beforeFrame(Editor& editor, int frame) {
  auto [from, to] = _timed.equal_range(frame);
  for (auto it = from; it != to; ++it) {
    if (!expand(it->second)) run(editor, it->second);
  }
  if (live()) readControl();
  if (_wait > 0) {
    --_wait;
    return;
  }
  // One step a frame (input needs frames between press and release); shots and markers ride along.
  while (!_queue.empty()) {
    const std::string step = _queue.front();
    _queue.pop_front();
    run(editor, step);
    if (!step.starts_with("@shot") && !step.starts_with("@done")) break;
  }
}

void Automation::afterRender(int width, int height) {
  if (_shot.empty()) return;
  savePng(_shot, width, height);
  _shot.clear();
}
