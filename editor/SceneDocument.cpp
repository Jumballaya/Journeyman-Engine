#include "SceneDocument.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "Entities.hpp"
#include "JsonFormat.hpp"
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

// As the file holds it: no editor ids or map files, numbers tidied as saving tidies them.
Json asOnDisk(Json value) {
  value.erase(kMapsKey);
  stripUids(value);
  wholeNumbersAsIntegers(value);
  return value;
}

// Sibling entities keyed by name and which of that name it is, in order.
struct Keyed {
  std::string name;
  int nth;  // 1 for the first "Bat", 2 for the second
  const Json* json;
  bool operator==(const Keyed& o) const { return name == o.name && nth == o.nth; }
  std::string label() const { return nth > 1 ? name + "[" + std::to_string(nth) + "]" : name; }
};

std::vector<Keyed> keyed(const Json& list) {
  std::vector<Keyed> out;
  std::map<std::string, int> seen;
  if (!list.is_array()) return out;
  for (const Json& e : list) {
    if (!e.is_object()) continue;
    std::string name = e.value("name", e.value("prefab", std::string()));
    const int nth = ++seen[name];
    out.push_back({std::move(name), nth, &e});
  }
  return out;
}

const Json* lookup(const std::vector<Keyed>& list, const Keyed& key) {
  const auto it = std::find(list.begin(), list.end(), key);
  return it == list.end() ? nullptr : it->json;
}

// `theirs` with the editor ids of `mine`'s matching entities (by name, children too): the selection survives.
Json carryUids(const Json& mine, Json theirs) {
  if (mine.contains(kUidKey)) theirs[kUidKey] = mine[kUidKey];
  if (!theirs.contains("children") || !mine.contains("children")) return theirs;
  const auto mineChildren = keyed(mine["children"]);
  Json children = Json::array();
  for (const Keyed& child : keyed(theirs["children"])) {
    const Json* match = lookup(mineChildren, child);
    children.push_back(match ? carryUids(*match, *child.json) : *child.json);
  }
  theirs["children"] = std::move(children);
  return theirs;
}

// Three-way, entity by entity: `base` as last on disk, `mine` (with ids) as
// edited here, `theirs` on disk now. A side's change wins over the other's
// sameness; changed (or deleted) on both, theirs wins and the entity joins `conflicts`.
Json mergeEntities(const Json& base, const Json& mine, const Json& theirs, std::vector<std::string>& conflicts) {
  const auto b = keyed(base), m = keyed(mine), t = keyed(theirs);
  // Each entity's outcome; nullopt: gone.
  const auto outcome = [&](const Keyed& key) -> std::optional<Json> {
    const Json* was = lookup(b, key);
    const Json* my = lookup(m, key);
    const Json* their = lookup(t, key);
    const std::optional<Json> myClean = my ? std::optional(asOnDisk(*my)) : std::nullopt;
    const bool mineChanged = was ? myClean != *was : my != nullptr;
    const bool theirsChanged = was ? !their || *their != *was : their != nullptr;
    if (!theirsChanged || (my && their && *myClean == *their)) return my ? std::optional(*my) : std::nullopt;
    if (mineChanged) conflicts.push_back(key.label());
    if (!their) return std::nullopt;
    return my ? carryUids(*my, *their) : *their;
  };
  std::vector<std::pair<Keyed, Json>> out;
  const bool diskKeptOrder = t.size() == b.size() && std::equal(t.begin(), t.end(), b.begin());
  if (diskKeptOrder) {
    // The disk added, removed and moved nothing: the order made here stands.
    for (const Keyed& key : m)
      if (auto e = outcome(key)) out.emplace_back(key, std::move(*e));
    for (const Keyed& key : t)  // deleted here: gone, unless changed on disk too (the file wins)
      if (!lookup(m, key))
        if (auto e = outcome(key)) out.emplace_back(key, std::move(*e));
  } else {
    for (const Keyed& key : t)
      if (auto e = outcome(key)) out.emplace_back(key, std::move(*e));
    // Added here: after the nearest earlier sibling still there, else before the nearest later one, else last.
    for (size_t i = 0; i < m.size(); ++i) {
      if (lookup(t, m[i]) || lookup(b, m[i])) continue;
      const auto place = [&](const Keyed& sibling) {
        return std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.first == sibling; });
      };
      auto at = out.end();
      bool placed = false;
      for (size_t j = i; j-- > 0 && !placed;) {
        if (auto found = place(m[j]); found != out.end()) at = found + 1, placed = true;
      }
      for (size_t j = i + 1; j < m.size() && !placed; ++j) {
        if (auto found = place(m[j]); found != out.end()) at = found, placed = true;
      }
      out.emplace(at, m[i], *m[i].json);
    }
    for (const Keyed& key : m)
      if (!lookup(t, key) && lookup(b, key)) outcome(key);  // deleted on disk: gone, or a conflict the file wins
  }
  Json list = Json::array();
  for (auto& [_, e] : out) list.push_back(std::move(e));
  return list;
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
  std::error_code ec;
  doc._diskTime = std::filesystem::last_write_time(project.abs(path), ec);
  doc._path = std::move(path);
  doc._onDisk = asOnDisk(doc._json);
  doc.assignUids(doc._json);
  doc.reindex();
  return doc;
}

SceneDocument SceneDocument::create(std::string path) {
  SceneDocument doc;
  doc._path = std::move(path);
  doc._json = {{"name", assetStem(doc._path)}, {"entities", Json::array()}};
  doc._savedCursor = kNeverSaved;
  doc._everSaved = false;
  doc.reindex();
  return doc;
}

std::string SceneDocument::title() const { return assetStem(_path); }

Json& SceneDocument::topLevel(Json& document, bool prefab) {
  Json& list = prefab ? document["children"] : document["entities"];
  if (!list.is_array()) list = Json::array();
  return list;
}

void SceneDocument::forEachEntity(Json& document, const std::function<void(Json& entity, Json* list)>& visit) const {
  std::function<void(Json&)> walk = [&](Json& list) {
    if (!list.is_array()) return;
    for (Json& e : list) {
      if (!e.is_object()) continue;
      visit(e, &list);
      if (e.contains("children")) walk(e["children"]);
    }
  };
  if (_prefab) {
    visit(document, nullptr);
    if (document.contains("children")) walk(document["children"]);
  } else if (document.contains("entities")) {
    walk(document["entities"]);
  }
}

Json* SceneDocument::locate(Json& document, EntityUid uid, Json** list) const {
  Json* found = nullptr;
  forEachEntity(document, [&](Json& e, Json* holder) {
    if (found || e.value(kUidKey, EntityUid{0}) != uid) return;
    found = &e;
    if (list) *list = holder;
  });
  return found;
}

const std::vector<SceneDocument::Node>& SceneDocument::nodes() const {
  if (_index.valid) return _index.nodes;
  std::vector<Node>& out = _index.nodes;
  out.clear();
  std::function<void(const Json&, EntityUid, int)> walk = [&](const Json& e, EntityUid parent, int depth) {
    if (!e.is_object()) return;
    const EntityUid uid = e.value(kUidKey, EntityUid{0});
    out.push_back({&e, uid, parent, depth});
    if (auto children = e.find("children"); children != e.end() && children->is_array()) {
      for (const Json& child : *children) walk(child, uid, depth + 1);
    }
  };
  if (_prefab) {
    walk(_json, 0, 0);
  } else if (auto list = _json.find("entities"); list != _json.end() && list->is_array()) {
    for (const Json& e : *list) walk(e, 0, 0);
  }
  _index.valid = true;
  return out;
}

int SceneDocument::indexOf(EntityUid uid) const {
  const auto& all = nodes();
  for (size_t i = 0; i < all.size(); ++i) {
    if (all[i].uid == uid) return static_cast<int>(i);
  }
  return -1;
}

const Json* SceneDocument::find(EntityUid uid) const {
  const int i = indexOf(uid);
  return i < 0 ? nullptr : nodes()[static_cast<size_t>(i)].json;
}

EntityUid SceneDocument::parentOf(EntityUid uid) const {
  const int i = indexOf(uid);
  return i < 0 ? 0 : nodes()[static_cast<size_t>(i)].parent;
}

std::vector<EntityUid> SceneDocument::childrenOf(EntityUid uid) const {
  std::vector<EntityUid> out;
  for (const Node& n : nodes()) {
    if (n.parent == uid && uid != 0) out.push_back(n.uid);
  }
  return out;
}

bool SceneDocument::isInside(EntityUid uid, EntityUid ancestor) const {
  for (EntityUid up = parentOf(uid); up != 0; up = parentOf(up)) {
    if (up == ancestor) return true;
  }
  return false;
}

std::string SceneDocument::displayName(size_t index) const {
  const Json& e = entity(index);
  if (_prefab && index == 0) return title();
  if (auto name = e.value("name", std::string()); !name.empty()) return name;
  if (auto prefab = e.value("prefab", std::string()); !prefab.empty()) return assetStem(prefab);
  return "Entity " + std::to_string(index + 1);
}

void SceneDocument::assignUids(Json& document) {
  // Copies made by hand (paste, duplicate) carry their original's uid: the first keeps it.
  std::set<EntityUid> seen;
  forEachEntity(document, [&](Json& e, Json*) {
    const EntityUid uid = e.value(kUidKey, EntityUid{0});
    if (uid == 0 || !seen.insert(uid).second) {
      e[kUidKey] = _nextUid++;
      seen.insert(e[kUidKey].get<EntityUid>());
    }
  });
}

void SceneDocument::edit(const std::string& label, const std::function<void(Json&)>& mutate,
                         const std::string& mergeKey) {
  Json before = _json;
  mutate(_json);
  assignUids(_json);
  reindex();
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
    forEachEntity(doc, [&](Json& e, Json*) {
      if (std::find(uids.begin(), uids.end(), e.value(kUidKey, EntityUid{0})) != uids.end()) mutate(e);
    });
  }, mergeKey);
}

namespace {

// A list's place for an insert: `at` if it's within it, else the end.
size_t insertionPoint(const Json& list, int at) {
  return at < 0 || at > static_cast<int>(list.size()) ? list.size() : static_cast<size_t>(at);
}

void dropEmptyChildren(Json& entity) {
  if (entity.contains("children") && entity["children"].is_array() && entity["children"].empty()) entity.erase("children");
}

}  // namespace

std::vector<EntityUid> SceneDocument::addEntities(std::vector<Json> entities, const std::string& label, EntityUid parent, int at) {
  std::vector<EntityUid> uids;
  for (Json& e : entities) {
    stripUids(e);  // copies get their own, children too
    uids.push_back(_nextUid++);
    e[kUidKey] = uids.back();
  }
  edit(label, [&](Json& doc) {
    Json* holder = parent ? locate(doc, parent) : nullptr;
    if (parent && !holder) return;
    Json& list = holder ? (*holder)["children"] : topLevel(doc, _prefab);
    if (!list.is_array()) list = Json::array();
    size_t where = insertionPoint(list, at);
    for (Json& e : entities) list.insert(list.begin() + static_cast<std::ptrdiff_t>(where++), std::move(e));
  });
  std::erase_if(uids, [this](EntityUid uid) { return indexOf(uid) < 0; });
  return uids;
}

void SceneDocument::removeEntities(const std::vector<EntityUid>& uids, const std::string& label) {
  edit(label, [&](Json& doc) {
    for (EntityUid uid : uids) {
      Json* list = nullptr;
      Json* e = locate(doc, uid, &list);
      if (!e || !list) continue;  // gone with an ancestor, or the prefab's root
      for (size_t i = 0; i < list->size(); ++i) {
        if ((*list)[i].value(kUidKey, EntityUid{0}) == uid) {
          list->erase(i);
          break;
        }
      }
    }
    forEachEntity(doc, [](Json& e, Json*) { dropEmptyChildren(e); });
  });
}

bool SceneDocument::moveEntities(const std::vector<EntityUid>& uids, EntityUid parent, int at, const std::string& label,
                                 const std::function<void(Json&)>& adjust) {
  // Not into itself, not the root; ancestors already moving carry their children along.
  std::vector<EntityUid> moving;
  for (EntityUid uid : uids) {
    const bool root = _prefab && indexOf(uid) == 0;
    const bool carried = std::any_of(uids.begin(), uids.end(), [&](EntityUid other) { return isInside(uid, other); });
    if (indexOf(uid) >= 0 && !root && !carried && uid != parent && !isInside(parent, uid)) moving.push_back(uid);
  }
  if (moving.empty()) return false;
  edit(label, [&](Json& doc) {
    // Where `at` points among the parent's children, counted without the movers.
    std::vector<Json> taken;
    Json* holder = parent ? locate(doc, parent) : nullptr;
    Json& target = holder ? (*holder)["children"] : topLevel(doc, _prefab);
    if (!target.is_array()) target = Json::array();
    int index = at;
    for (int i = 0; i < static_cast<int>(target.size()) && i < at; ++i) {
      if (std::find(moving.begin(), moving.end(), target[static_cast<size_t>(i)].value(kUidKey, EntityUid{0})) != moving.end()) --index;
    }
    for (EntityUid uid : moving) {
      Json* list = nullptr;
      Json* e = locate(doc, uid, &list);
      if (!e || !list) continue;
      Json moved = *e;
      if (adjust) adjust(moved);
      for (size_t i = 0; i < list->size(); ++i) {
        if ((*list)[i].value(kUidKey, EntityUid{0}) == uid) {
          list->erase(i);
          break;
        }
      }
      taken.push_back(std::move(moved));
    }
    // Erasing may have moved the target list's storage: find it again.
    Json* again = parent ? locate(doc, parent) : nullptr;
    Json& into = again ? (*again)["children"] : topLevel(doc, _prefab);
    if (!into.is_array()) into = Json::array();
    size_t where = insertionPoint(into, index);
    for (Json& e : taken) into.insert(into.begin() + static_cast<std::ptrdiff_t>(where++), std::move(e));
    forEachEntity(doc, [](Json& e, Json*) { dropEmptyChildren(e); });
  });
  return true;
}

std::vector<EntityUid> SceneDocument::duplicate(const std::vector<EntityUid>& uids, const std::string& label,
                                                const std::function<void(Json&)>& adjust) {
  std::vector<EntityUid> copies;
  edit(label, [&](Json& doc) {
    for (EntityUid uid : uids) {
      Json* list = nullptr;
      Json* e = locate(doc, uid, &list);
      if (!e || !list) continue;
      Json copy = *e;
      stripUids(copy);
      copies.push_back(_nextUid++);
      copy[kUidKey] = copies.back();
      if (adjust) adjust(copy);
      for (size_t i = 0; i < list->size(); ++i) {
        if ((*list)[i].value(kUidKey, EntityUid{0}) == uid) {
          list->insert(list->begin() + static_cast<std::ptrdiff_t>(i + 1), std::move(copy));
          break;
        }
      }
    }
  });
  std::erase_if(copies, [this](EntityUid uid) { return indexOf(uid) < 0; });
  return copies;
}

void SceneDocument::undo() {
  if (!canUndo()) return;
  _json = _history[--_cursor].before;
  reindex();
  ++_revision;
}

void SceneDocument::redo() {
  if (!canRedo()) return;
  _json = _history[_cursor++].after;
  reindex();
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

std::string SceneDocument::reference(EntityUid uid) const {
  std::vector<std::string> parts;
  for (EntityUid at = uid; at != 0; at = parentOf(at)) {
    const int index = indexOf(at);
    if (index < 0 || (_prefab && depth(static_cast<size_t>(index)) == 0)) break;
    const std::string name = displayName(static_cast<size_t>(index));
    const EntityUid parent = parentOf(at);
    int same = 0;  // earlier siblings with this name
    for (int i = 0; i < index; ++i) {
      if (nodes()[i].parent == parent && displayName(static_cast<size_t>(i)) == name) ++same;
    }
    parts.push_back(same == 0 ? name : name + "[" + std::to_string(same + 1) + "]");
  }
  std::string out = _path;
  for (auto part = parts.rbegin(); part != parts.rend(); ++part) out += (part == parts.rbegin() ? "#" : "/") + *part;
  return out;
}

std::string SceneDocument::spotReference(glm::vec2 world) const {
  const glm::ivec2 at(glm::round(world));
  return _path + "@" + std::to_string(at.x) + "," + std::to_string(at.y);
}

std::string SceneDocument::serialized() const {
  Json clean = _json;
  clean.erase(kMapsKey);
  stripUids(clean);
  return formatJson(clean);
}

const Json* SceneDocument::mapFile(const std::string& path) const {
  auto maps = _json.find(kMapsKey);
  if (maps == _json.end() || !maps->contains(path)) return nullptr;
  return &(*maps)[path];
}

std::vector<std::string> SceneDocument::mapFiles() const {
  std::vector<std::string> out;
  const Json maps = _json.value(kMapsKey, Json::object());  // named: items() of a temporary dangles
  for (const auto& [path, _] : maps.items()) out.push_back(path);
  return out;
}

void SceneDocument::forgetMapFile(const std::string& path) {
  if (auto maps = _json.find(kMapsKey); maps != _json.end()) {
    maps->erase(path);
    reindex();
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
  const Json maps = _json.value(kMapsKey, Json::object());  // named: items() of a temporary dangles
  for (const auto& [path, map] : maps.items()) {
    const std::string text = tiled::serializeMap(map);
    if (project.readText(path) != text && !project.writeText(path, text, error)) return false;
  }
  _savedCursor = _cursor;
  _everSaved = true;
  _onDisk = asOnDisk(_json);
  _diskProblem.clear();
  std::error_code ec;
  _diskTime = std::filesystem::last_write_time(project.abs(_path), ec);
  return true;
}

std::optional<Json> SceneDocument::changedOnDisk(const Project& project) {
  if (!_everSaved) return std::nullopt;  // nothing on disk yet
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(project.abs(_path), ec);
  if (ec || time == _diskTime) return std::nullopt;
  Json disk;
  try {
    disk = Json::parse(project.readText(_path));
  } catch (const Json::parse_error& e) {  // mid-write, or broken: looked at again on its next change
    _diskProblem = e.what();
    return std::nullopt;
  }
  _diskProblem = disk.is_object() ? "" : "it isn't a JSON object";
  if (!_diskProblem.empty()) return std::nullopt;
  _diskTime = time;
  wholeNumbersAsIntegers(disk);
  if (disk == _onDisk) return std::nullopt;  // what this document last read or wrote (our save, a touch)
  return disk;
}

std::vector<std::string> SceneDocument::takeDiskVersion(Json disk) {
  wholeNumbersAsIntegers(disk);
  std::vector<std::string> conflicts;
  const char* list = _prefab ? "children" : "entities";
  Json merged = disk;
  // Outside the entities (a prefab's components, a scene's name), key by key the same way.
  const Json mine = asOnDisk(_json);
  std::set<std::string> keys;
  for (const Json* side : std::initializer_list<const Json*>{&mine, &_onDisk, &disk})
    for (auto& [key, _] : side->items()) keys.insert(key);
  for (const std::string& key : keys) {
    const Json was = _onDisk.value(key, Json()), my = mine.value(key, Json()), their = disk.value(key, Json());
    if (key == list || my == was || my == their) continue;
    if (their != was) {
      conflicts.push_back(std::string(_prefab ? "the prefab's " : "the scene's ") + key);
    } else if (my.is_null()) {
      merged.erase(key);
    } else {
      merged[key] = my;
    }
  }
  Json entities = mergeEntities(_onDisk.value(list, Json::array()), _json.value(list, Json::array()), disk.value(list, Json::array()), conflicts);
  if (!entities.empty() || disk.contains(list)) merged[list] = std::move(entities);
  if (_prefab && _json.contains(kUidKey)) merged[kUidKey] = _json[kUidKey];
  if (auto maps = _json.find(kMapsKey); maps != _json.end()) merged[kMapsKey] = *maps;  // map files being painted stay
  _onDisk = std::move(disk);
  edit("Change on Disk", [&](Json& whole) { whole = std::move(merged); });
  // Saved when nothing of this side's survived the merge; Undo brings back the edits as they were.
  _savedCursor = asOnDisk(_json) == _onDisk ? _cursor : kNeverSaved;
  return conflicts;
}
