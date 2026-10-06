#include "AssetDocument.hpp"

#include <imgui.h>

#include "Entities.hpp"

namespace fs = std::filesystem;

std::unique_ptr<AssetDocument> AssetDocument::load(const Project& project, std::string path, std::string& error) {
  std::error_code ec;
  if (!fs::exists(project.abs(path), ec)) {
    error = "Couldn't find " + path;
    return nullptr;
  }
  const std::string text = project.readText(path);
  auto doc = std::make_unique<AssetDocument>();
  if (path.ends_with(".json") || path.ends_with(".tsj") || path.ends_with(".tmj")) {  // Tiled's are JSON too
    doc->_value = Json::parse(text.empty() ? "{}" : text, nullptr, false);
    if (doc->_value.is_discarded()) {
      error = path + " isn't valid JSON, so it opens as text in your code editor.";
      return nullptr;
    }
  } else {
    doc->_value = text;
  }
  doc->_path = std::move(path);
  doc->_saved = doc->_value;
  doc->_endsWithNewline = text.empty() || text.ends_with('\n');
  doc->_diskTime = fs::last_write_time(project.abs(doc->_path), ec);
  return doc;
}

std::string AssetDocument::title() const { return fs::path(_path).filename().string(); }

void AssetDocument::changed() {
  ++_revision;
  _changedAt = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
}

void AssetDocument::edit(const std::string& label, const std::function<void(Json&)>& mutate, const std::string& mergeKey) {
  Json before = _value;
  mutate(_value);
  if (_value == before) return;
  _history.resize(_cursor);  // a new change drops the redo branch
  if (!mergeKey.empty() && !_history.empty() && _history.back().mergeKey == mergeKey) {
    _history.back().after = _value;
  } else {
    _history.push_back({label, std::move(before), _value, mergeKey});
    if (_history.size() > 300) _history.erase(_history.begin());
  }
  _cursor = _history.size();
  changed();
}

void AssetDocument::setText(const std::string& text, const std::string& label, const std::string& mergeKey) {
  edit(label, [&](Json& v) { v = text; }, mergeKey);
}

void AssetDocument::undo() {
  if (!canUndo()) return;
  _value = _history[--_cursor].before;
  changed();
}

void AssetDocument::redo() {
  if (!canRedo()) return;
  _value = _history[_cursor++].after;
  changed();
}

std::string AssetDocument::serialized() const {
  if (isText()) return text();
  Json clean = _value;
  wholeNumbersAsIntegers(clean);
  return clean.dump(2) + (_endsWithNewline ? "\n" : "");
}

bool AssetDocument::save(const Project& project, std::string& error) {
  if (!project.writeText(_path, serialized(), error)) return false;
  _saved = _value;
  std::error_code ec;
  _diskTime = fs::last_write_time(project.abs(_path), ec);
  return true;
}

bool AssetDocument::reloadIfChanged(const Project& project) {
  std::error_code ec;
  const auto time = fs::last_write_time(project.abs(_path), ec);
  if (ec || time == _diskTime || dirty()) return false;
  _diskTime = time;
  std::string error;
  auto fresh = load(project, _path, error);
  if (!fresh || fresh->_value == _value) return false;
  // Someone else's edit: it becomes an undoable step, so nothing is lost.
  edit("Change on Disk", [&](Json& v) { v = fresh->_value; });
  _saved = _value;
  return true;
}
