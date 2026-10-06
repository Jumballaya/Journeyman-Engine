#include "Automation.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <glad/gl.h>
#include <imgui.h>

#include "Editor.hpp"
#include "LogBook.hpp"
#include "panels/Panels.hpp"
#include "stb_image_write.h"

namespace {

}  // namespace

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

// Simulated input for automation: "@mouse 400 300", "@down", "@key W", "@select Player".
void simulate(Editor& editor, const std::string& action) {
  ImGuiIO& io = ImGui::GetIO();
  std::istringstream in(action);
  std::string verb;
  in >> verb;
  if (verb == "@mouse") {
    float x = 0, y = 0;
    in >> x >> y;
    io.AddMousePosEvent(x, y);
  } else if (verb == "@down" || verb == "@up") {
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, verb == "@down");
  } else if (verb == "@rdown" || verb == "@rup") {
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, verb == "@rdown");
  } else if (verb == "@wheel") {
    float dy = 0;
    in >> dy;
    io.AddMouseWheelEvent(0, dy);
  } else if (verb == "@ctrl" || verb == "@shift" || verb == "@release") {
    const bool down = verb != "@release";
    if (verb == "@ctrl" || !down) io.AddKeyEvent(ImGuiMod_Ctrl, down && verb == "@ctrl");
    if (verb == "@shift" || !down) io.AddKeyEvent(ImGuiMod_Shift, down && verb == "@shift");
  } else if (verb == "@key") {
    std::string name;
    in >> name;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
      if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(k))) {
        io.AddKeyEvent(static_cast<ImGuiKey>(k), true);
        io.AddKeyEvent(static_cast<ImGuiKey>(k), false);
      }
    }
  } else if (verb == "@type") {
    std::string text;
    std::getline(in >> std::ws, text);
    io.AddInputCharactersUTF8(text.c_str());
  } else if (verb == "@import") {
    std::string file;
    std::getline(in >> std::ws, file);  // paths may have spaces
    editor.importFiles({std::filesystem::path(file)}, editor.assetsFolder());
  } else if (verb == "@add") {
    std::string path;
    std::getline(in >> std::ws, path);  // paths may have spaces
    editor.instantiateAsset(path, editor.scenePanel().viewCenter());
  } else if (verb == "@apply") {
    std::string path;
    std::getline(in >> std::ws, path);  // paths may have spaces
    editor.applyAssetToEntity(editor.primary(), path);
  } else if (verb == "@live") {
    std::string tag;
    in >> tag;
    if (HostedEngine* game = editor.game()) {
      auto found = game->engine().getWorld().findWithTag(tag);
      if (!found.empty()) editor.selectLive(*found.begin());
    }
  } else if (verb == "@makeprefab") {
    editor.createPrefab(editor.primary(), editor.assetsFolderForPrefabs());
  } else if (verb == "@move") {
    std::string from, to;
    in >> from >> to;
    editor.moveAsset(from, to);
  } else if (verb == "@reveal") {
    std::string path;
    std::getline(in >> std::ws, path);  // paths may have spaces
    editor.revealAsset(path);
  } else if (verb == "@open") {
    std::string path;
    std::getline(in >> std::ws, path);  // paths may have spaces
    editor.openAsset(path);
  } else if (verb == "@inspect") {
    std::string path;
    std::getline(in >> std::ws, path);  // paths may have spaces
    editor.inspectAsset(path);
  } else if (verb == "@select") {
    std::string name;
    std::getline(in >> std::ws, name);
    if (SceneDocument* scene = editor.scene()) {
      for (size_t i = 0; i < scene->size(); ++i) {
        if (scene->displayName(i) == name) editor.select(scene->uid(i));
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

void Automation::push(const std::string& step) {
  std::istringstream in(step);
  std::string verb;
  in >> verb;
  float x = 0, y = 0, x2 = 0, y2 = 0;
  auto at = [](float px, float py) { return "@mouse " + std::to_string(px) + " " + std::to_string(py); };
  if (verb == "@click" && in >> x >> y) {
    for (const std::string& s : {at(x, y), std::string("@down"), std::string("@up")}) _queue.push_back(s);
  } else if (verb == "@dblclick" && in >> x >> y) {
    for (const std::string& s : {at(x, y), std::string("@down"), std::string("@up"), std::string("@down"), std::string("@up")}) _queue.push_back(s);
  } else if (verb == "@rclick" && in >> x >> y) {
    for (const std::string& s : {at(x, y), std::string("@rdown"), std::string("@rup")}) _queue.push_back(s);
  } else if (verb == "@drag" && in >> x >> y >> x2 >> y2) {
    // Down, a few moves on the way (drag thresholds, drop targets), up.
    _queue.push_back(at(x, y));
    _queue.push_back("@down");
    for (int i = 1; i <= 8; ++i) _queue.push_back(at(x + (x2 - x) * i / 8.0f, y + (y2 - y) * i / 8.0f));
    _queue.push_back(at(x2, y2));
    _queue.push_back("@up");
  } else {
    _queue.push_back(step);
  }
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
    if (!line.empty() && line[0] != '#') push(line);
  }
}

void Automation::run(Editor& editor, const std::string& step) {
  if (step.starts_with("@wait")) {
    _wait = std::max(0, std::atoi(step.substr(5).c_str()));
  } else if (step.starts_with("@shot ")) {
    _shot = step.substr(6);
  } else if (step.starts_with("@done ")) {
    std::ofstream(_control / "done", std::ios::trunc) << step.substr(6);
  } else if (step.starts_with("@")) {
    simulate(editor, step);
  } else if (!editor.commands().run(step)) {
    LogBook::instance().add(LogBook::Level::Warning, LogBook::Source::Editor, "automation: '" + step + "' didn't run");
  }
}

void Automation::beforeFrame(Editor& editor, int frame) {
  auto [from, to] = _timed.equal_range(frame);
  for (auto it = from; it != to; ++it) {
    if (it->second.starts_with("@click") || it->second.starts_with("@drag") || it->second.starts_with("@dblclick") ||
        it->second.starts_with("@rclick")) {
      push(it->second);
    } else {
      run(editor, it->second);
    }
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
