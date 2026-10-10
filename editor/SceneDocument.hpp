#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Project.hpp"

using EntityUid = uint64_t;  // stable while editing; never saved

// A scene or prefab file being edited, and its undo history. Scenes hold a
// list of entities ({"name", "components"} or {"name", "prefab", "overrides"});
// a prefab is one entity ({"components", "tags"}). Any entity may hold others
// in its "children" (positioned relative to it). Entities are addressed by
// index in depth-first order (parents before their children) or by uid.
// Every change goes through edit(), so it can be undone and the preview knows
// what changed.
class SceneDocument {
 public:
  static std::optional<SceneDocument> load(const Project& project, std::string path, std::string& error);
  // A new, empty scene at `path` (not yet written).
  static SceneDocument create(std::string path);

  const std::string& path() const { return _path; }
  std::string title() const;  // "level1"
  bool isPrefab() const { return _prefab; }

  size_t size() const { return nodes().size(); }
  // An entity's entry, its "children" included.
  const Json& entity(size_t index) const { return *nodes()[index].json; }
  EntityUid uid(size_t index) const { return nodes()[index].uid; }
  // How deep it nests: 0 for the scene's entities and a prefab's root.
  int depth(size_t index) const { return nodes()[index].depth; }
  // Its parent's uid, or 0 for a top-level entity (and a prefab's root).
  EntityUid parentOf(EntityUid uid) const;
  std::vector<EntityUid> childrenOf(EntityUid uid) const;
  // Whether `ancestor` holds `uid`, at any depth.
  bool isInside(EntityUid uid, EntityUid ancestor) const;
  // -1 if no entity has it (deleted, or undone away).
  int indexOf(EntityUid uid) const;
  const Json* find(EntityUid uid) const;
  std::string displayName(size_t index) const;
  // How to name an entity outside the editor (to an agent): the file, then its
  // path of names, "scenes/level1.scene.json#Hero/Sword" ("Bat[2]" for the
  // second of siblings sharing a name). A prefab itself is just its file.
  std::string reference(EntityUid uid) const;
  // A spot in it, for an agent: "scenes/level1.scene.json@120,-40" (world units, y up).
  std::string spotReference(glm::vec2 world) const;

  // One undoable change, labeled for the Edit menu ("Move Player"). `mutate`
  // gets the whole document; entities it adds get fresh uids. Consecutive
  // edits with the same non-empty `mergeKey` fold into one step: give each
  // gesture (one drag, one paint stroke) its own key (see gestureKey).
  void edit(const std::string& label, const std::function<void(Json& document)>& mutate,
            const std::string& mergeKey = {});
  // Mutates one entity, or several in one step (the prefab itself in a prefab).
  void editEntity(EntityUid uid, const std::string& label, const std::function<void(Json& entity)>& mutate,
                  const std::string& mergeKey = {}) {
    editEntities({uid}, label, mutate, mergeKey);
  }
  void editEntities(const std::vector<EntityUid>& uids, const std::string& label,
                    const std::function<void(Json& entity)>& mutate, const std::string& mergeKey = {});
  // Adds entities (with their children) under `parent` (0: the scene's top
  // level, a prefab's root), appended or at index `at` among its children.
  // Returns their uids.
  std::vector<EntityUid> addEntities(std::vector<Json> entities, const std::string& label, EntityUid parent = 0, int at = -1);
  EntityUid addEntity(Json entity, const std::string& label, EntityUid parent = 0, int at = -1) {
    auto added = addEntities({std::move(entity)}, label, parent, at);
    return added.empty() ? 0 : added.front();
  }
  // Removes entities with everything inside them (never a prefab's root).
  void removeEntities(const std::vector<EntityUid>& uids, const std::string& label);
  // Moves entities under `parent` (as for addEntities), each passed to `adjust`
  // on the way (to keep where it stands). Entities can't move into themselves
  // or a prefab's root anywhere; false (nothing changes) if none could.
  bool moveEntities(const std::vector<EntityUid>& uids, EntityUid parent, int at, const std::string& label,
                    const std::function<void(Json& entity)>& adjust = {});
  // Copies each entity (children and all) right after it, passing each copy to
  // `adjust`; returns the copies' uids.
  std::vector<EntityUid> duplicate(const std::vector<EntityUid>& uids, const std::string& label,
                                   const std::function<void(Json& copy)>& adjust = {});

  bool canUndo() const { return _cursor > 0; }
  bool canRedo() const { return _cursor < _history.size(); }
  std::string undoLabel() const { return canUndo() ? _history[_cursor - 1].label : ""; }
  std::string redoLabel() const { return canRedo() ? _history[_cursor].label : ""; }
  void undo();
  void redo();
  // The history as labels, oldest first, and how many steps are applied.
  std::vector<std::string> historyLabels() const;
  size_t historyPosition() const { return _cursor; }
  // Undoes or redoes until `position` steps are applied.
  void jumpTo(size_t position);

  bool dirty() const { return _cursor != _savedCursor || _savedCursor == kNeverSaved; }
  bool save(const Project& project, std::string& error);
  // Saves under a new path from now on (Save As); history is kept.
  bool saveAs(const Project& project, std::string path, std::string& error);
  // Never written to disk yet (a new scene).
  bool unsaved() const { return !_everSaved; }
  // The file's text as it would be saved (no editor ids or map files).
  std::string serialized() const;
  // Another program's change to the file (an agent's edit) since it was last
  // loaded, saved or reported here: the file's JSON, or nullopt if it holds
  // what this document last wrote. Each change is reported once; a file that
  // isn't valid JSON yet (half-written) is reported once it is.
  std::optional<Json> changedOnDisk(const Project& project);
  // Takes the file's version as one undoable step ("Change on Disk") that
  // counts as saved: unsaved edits made here stay one Undo away.
  void takeDiskVersion(Json document);

  // Rows of a map file being painted, or null if it isn't loaded here.
  const Json* mapFile(const std::string& path) const;
  std::vector<std::string> mapFiles() const;
  // Drops the copy of a map file (not an undoable step): the file changed elsewhere and wins.
  void forgetMapFile(const std::string& path);

  // Bumps on every change (edit, undo, redo); the preview compares it.
  uint64_t revision() const { return _revision; }

 private:
  static constexpr size_t kNeverSaved = static_cast<size_t>(-1);
  struct Step {
    std::string label;
    Json before, after;
    std::string mergeKey;
  };

  std::string _path;
  bool _prefab = false;
  bool _everSaved = true;         // false for a new scene until its first save
  std::filesystem::file_time_type _diskTime{};  // the file's, when last loaded, saved or reported
  Json _json;
  std::vector<Step> _history;
  size_t _cursor = 0;  // steps applied
  size_t _savedCursor = 0;  // kNeverSaved when no step holds what's on disk
  uint64_t _revision = 0;
  EntityUid _nextUid = 1;

  struct Node {
    const Json* json;
    EntityUid uid, parent;
    int depth;
  };
  // Depth-first, pointing into _json: rebuilt on first use after a change, and
  // after a copy or move (which re-homes the JSON).
  struct Index {
    std::vector<Node> nodes;
    bool valid = false;
    Index() = default;
    Index(const Index&) {}
    Index& operator=(const Index&) {
      nodes.clear();
      valid = false;
      return *this;
    }
  };
  mutable Index _index;
  const std::vector<Node>& nodes() const;

  // The top-level list (the prefab's root: its "children").
  static Json& topLevel(Json& document, bool prefab);
  // Every entity in `document`, depth-first, as (entity, the list holding it or null for a prefab's root).
  void forEachEntity(Json& document, const std::function<void(Json& entity, Json* list)>& visit) const;
  // The entity with `uid` inside `document`, and the list holding it (null for a prefab's root).
  Json* locate(Json& document, EntityUid uid, Json** list = nullptr) const;
  void assignUids(Json& document);
  void reindex() { _index.valid = false; }
};

// A merge key unique to one gesture: the same while one drag lasts, new for the
// next. Call every frame the gesture continues, `started` on its first.
std::string gestureKey(const std::string& what, bool started);

// The editor-only key carrying an entity's uid inside the document.
inline constexpr const char* kUidKey = "__editorUid";
// Map files (Tiled .tmj) being painted ride inside the document under this key,
// {"assets/maps/town.tmj": {...}}, so they undo with the scene; save() writes
// them back to their files.
inline constexpr const char* kMapsKey = "__maps";
