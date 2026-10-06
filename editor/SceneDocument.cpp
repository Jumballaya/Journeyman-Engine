#include "SceneDocument.hpp"

#include <algorithm>

#include "Entities.hpp"
#include "TiledFiles.hpp"

namespace {

void stripUids(Json& value) {
  if (value.is_object()) {
    value.erase(kUidKey);
    for (auto& [_, v] : value.items()) stripUids(v);
  } else if (value.is_array()) {
    for (auto& v : value) stripUids(v);
  }
}

}  // namespace

std::string gestureKey(const std::string& what, bool started) {
  static uint64_t gesture = 0;
  if (started) ++gesture;
  return what + "#" + std::to_string(gesture);
}

std::optional<SceneDocument> SceneDocument::load(const Project& project, std::string path, std::string& error) {
  const std::string text = project.readText(path);
  if (text.empty()) {
    error = "Couldn't read " + path;
    return std::nullopt;
  }
  SceneDocument doc;
  try {
    doc._json = Json::parse(text);
  } catch (const std::exception& e) {
    error = path + " is not valid JSON: " + e.what();
    return std::nullopt;
  }
  doc._prefab = path.ends_with(".prefab.json");
  const char* list = doc._prefab ? "components" : "entities";
  const Json empty = doc._prefab ? Json::object() : Json::array();
  if (doc._json.is_object() && doc._json[list].is_null()) doc._json[list] = empty;
  if (!doc._json.is_object() || doc._json[list].type() != empty.type()) {
    error = path + " isn't a " + (doc._prefab ? "prefab" : "scene") + " file.";
    return std::nullopt;
  }
  doc._path = std::move(path);
  doc._endsWithNewline = text.ends_with('\n');
  doc.assignUids(doc._json);
  return doc;
}

SceneDocument SceneDocument::create(std::string path) {
  SceneDocument doc;
  doc._path = std::move(path);
  doc._json = {{"name", assetStem(doc._path)}, {"entities", Json::array()}};
  doc._savedCursor = kNeverSaved;
  doc._everSaved = false;
  return doc;
}

std::string SceneDocument::title() const { return assetStem(_path); }

const Json& SceneDocument::entities() const {
  static const Json empty = Json::array();
  auto it = _json.find("entities");
  return it == _json.end() ? empty : *it;
}

size_t SceneDocument::size() const { return _prefab ? 1 : entities().size(); }

const Json& SceneDocument::entity(size_t index) const { return _prefab ? _json : entities()[index]; }

EntityUid SceneDocument::uid(size_t index) const { return entity(index).value(kUidKey, EntityUid{0}); }

int SceneDocument::indexOf(EntityUid uid) const {
  for (size_t i = 0; i < size(); ++i) {
    if (this->uid(i) == uid) return static_cast<int>(i);
  }
  return -1;
}

const Json* SceneDocument::find(EntityUid uid) const {
  const int i = indexOf(uid);
  return i < 0 ? nullptr : &entity(static_cast<size_t>(i));
}

std::string SceneDocument::displayName(size_t index) const {
  const Json& e = entity(index);
  if (_prefab) return title();
  if (auto name = e.value("name", std::string()); !name.empty()) return name;
  if (auto prefab = e.value("prefab", std::string()); !prefab.empty()) return assetStem(prefab);
  return "Entity " + std::to_string(index + 1);
}

void SceneDocument::assignUids(Json& document) {
  auto give = [this](Json& e) {
    if (e.is_object() && !e.contains(kUidKey)) e[kUidKey] = _nextUid++;
  };
  if (_prefab) {
    give(document);
    return;
  }
  for (auto& e : document["entities"]) give(e);
}

void SceneDocument::edit(const std::string& label, const std::function<void(Json&)>& mutate,
                         const std::string& mergeKey) {
  Json before = _json;
  mutate(_json);
  assignUids(_json);
  if (_json == before) return;
  ++_revision;

  _history.resize(_cursor);  // a new change drops the redo branch
  if (_savedCursor > _cursor) _savedCursor = kNeverSaved;
  // Never fold into the saved step, so undo can return to what's on disk.
  if (!mergeKey.empty() && !_history.empty() && _history.back().mergeKey == mergeKey && _savedCursor != _cursor) {
    _history.back().after = _json;
    return;
  }
  _history.push_back({label, std::move(before), _json, mergeKey});
  _cursor = _history.size();
  if (_history.size() > 400) {  // keep memory bounded on long sessions
    _history.erase(_history.begin());
    --_cursor;
    _savedCursor = _savedCursor == 0 || _savedCursor == kNeverSaved ? kNeverSaved : _savedCursor - 1;
  }
}

void SceneDocument::editEntities(const std::vector<EntityUid>& uids, const std::string& label,
                                 const std::function<void(Json&)>& mutate, const std::string& mergeKey) {
  edit(label, [&](Json& doc) {
    if (_prefab) {
      mutate(doc);
      return;
    }
    for (auto& e : doc["entities"]) {
      if (std::find(uids.begin(), uids.end(), e.value(kUidKey, EntityUid{0})) != uids.end()) mutate(e);
    }
  }, mergeKey);
}

EntityUid SceneDocument::addEntity(Json entity, const std::string& label, int at) {
  if (_prefab) return 0;
  const EntityUid uid = _nextUid++;
  entity[kUidKey] = uid;
  edit(label, [&](Json& doc) {
    Json& list = doc["entities"];
    if (at < 0 || at >= static_cast<int>(list.size())) {
      list.push_back(std::move(entity));
    } else {
      list.insert(list.begin() + at, std::move(entity));
    }
  });
  return uid;
}

void SceneDocument::removeEntities(const std::vector<EntityUid>& uids, const std::string& label) {
  if (_prefab) return;
  edit(label, [&](Json& doc) {
    Json& list = doc["entities"];
    for (size_t i = list.size(); i-- > 0;) {
      if (std::find(uids.begin(), uids.end(), list[i].value(kUidKey, EntityUid{0})) != uids.end()) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
  });
}

void SceneDocument::undo() {
  if (!canUndo()) return;
  _json = _history[--_cursor].before;
  ++_revision;
}

void SceneDocument::redo() {
  if (!canRedo()) return;
  _json = _history[_cursor++].after;
  ++_revision;
}

std::vector<std::string> SceneDocument::historyLabels() const {
  std::vector<std::string> labels;
  for (const Step& step : _history) labels.push_back(step.label);
  return labels;
}

void SceneDocument::jumpTo(size_t position) {
  position = std::min(position, _history.size());
  while (_cursor > position) undo();
  while (_cursor < position) redo();
}

std::string SceneDocument::serialized() const {
  Json clean = _json;
  clean.erase(kMapsKey);
  stripUids(clean);
  wholeNumbersAsIntegers(clean);
  return clean.dump(2) + (_endsWithNewline ? "\n" : "");
}

const Json* SceneDocument::mapFile(const std::string& path) const {
  auto maps = _json.find(kMapsKey);
  if (maps == _json.end() || !maps->contains(path)) return nullptr;
  return &(*maps)[path];
}

std::vector<std::string> SceneDocument::mapFiles() const {
  std::vector<std::string> out;
  for (const auto& [path, _] : _json.value(kMapsKey, Json::object()).items()) out.push_back(path);
  return out;
}

void SceneDocument::forgetMapFile(const std::string& path) {
  if (auto maps = _json.find(kMapsKey); maps != _json.end()) {
    maps->erase(path);
    ++_revision;
  }
}

bool SceneDocument::saveAs(const Project& project, std::string path, std::string& error) {
  const std::string previous = std::exchange(_path, std::move(path));
  if (!_prefab) _json["name"] = assetStem(_path);
  if (save(project, error)) return true;
  _path = previous;
  return false;
}

bool SceneDocument::save(const Project& project, std::string& error) {
  if (!project.writeText(_path, serialized(), error)) return false;
  for (const auto& [path, map] : _json.value(kMapsKey, Json::object()).items()) {
    const std::string text = tiled::serializeMap(map);
    if (project.readText(path) != text && !project.writeText(path, text, error)) return false;
  }
  _savedCursor = _cursor;
  _everSaved = true;
  return true;
}
