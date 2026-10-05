#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Project.hpp"

using EntityUid = uint64_t;  // stable while editing; never saved

// A scene or prefab file being edited, and its undo history. Scenes hold a
// list of entities ({"name", "components"} or {"name", "prefab", "overrides"});
// a prefab is a single entity ({"components", "tags"}). Every change goes
// through edit(), so it can be undone and the preview knows what changed.
class SceneDocument {
 public:
  static std::optional<SceneDocument> load(const Project& project, std::string path, std::string& error);
  // A new, empty scene at `path` (not yet written).
  static SceneDocument create(std::string path);

  const std::string& path() const { return _path; }
  std::string title() const;  // "level1"
  bool isPrefab() const { return _prefab; }

  size_t size() const;
  const Json& entity(size_t index) const;
  EntityUid uid(size_t index) const;
  // -1 if no entity has it (deleted, or undone away).
  int indexOf(EntityUid uid) const;
  const Json* find(EntityUid uid) const;
  std::string displayName(size_t index) const;

  // One undoable change, labeled for the Edit menu ("Move Player"). `mutate`
  // gets the whole document; entities it adds get fresh uids. Consecutive
  // edits with the same non-empty `mergeKey` fold into one step: give each
  // gesture (one drag, one paint stroke) its own key (see gestureKey).
  void edit(const std::string& label, const std::function<void(Json& document)>& mutate,
            const std::string& mergeKey = {});
  // Shortcut: mutate one entity, or several in one step.
  void editEntity(EntityUid uid, const std::string& label, const std::function<void(Json& entity)>& mutate,
                  const std::string& mergeKey = {});
  void editEntities(const std::vector<EntityUid>& uids, const std::string& label,
                    const std::function<void(Json& entity)>& mutate, const std::string& mergeKey = {});
  // Appends (or inserts at `at`) an entity; returns its uid.
  EntityUid addEntity(Json entity, const std::string& label, int at = -1);
  void removeEntities(const std::vector<EntityUid>& uids, const std::string& label);

  bool canUndo() const { return _cursor > 0; }
  bool canRedo() const { return _cursor < _history.size(); }
  std::string undoLabel() const;
  std::string redoLabel() const;
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
  bool unsaved() const { return _savedCursor == kNeverSaved && !_everSaved; }
  // The file's text as it would be saved (no editor ids or map files).
  std::string serialized() const;
  // Rows of a map file being painted, or null if it isn't loaded here.
  const Json* mapFile(const std::string& path) const;
  std::vector<std::string> mapFiles() const;

  // Bumps on every change (edit, undo, redo); the preview compares it.
  uint64_t revision() const { return _revision; }

 private:
  static constexpr size_t kNeverSaved = static_cast<size_t>(-1);
  struct Step {
    std::string label;
    Json before, after;
    std::string mergeKey;
    double time = 0;
  };

  std::string _path;
  bool _prefab = false;
  bool _endsWithNewline = true;  // kept as found, so saves don't churn the last line
  bool _everSaved = true;         // false for a new scene until its first save
  Json _json;
  std::vector<Step> _history;
  size_t _cursor = 0;  // steps applied
  size_t _savedCursor = 0;
  uint64_t _revision = 0;
  EntityUid _nextUid = 1;

  Json& entities();
  const Json& entities() const;
  void assignUids(Json& document);
};

// A merge key unique to the gesture that `active` is part of: the same while
// one drag lasts, new for the next. Call every frame the gesture continues.
std::string gestureKey(const std::string& what, bool started);

// The editor-only key carrying an entity's uid inside the document.
inline constexpr const char* kUidKey = "__editorUid";
// Map files (.txt rows) being painted ride inside the document under this key,
// {"assets/maps/town.txt": ["row", ...]}, so they undo with the scene; save()
// writes them back to their files.
inline constexpr const char* kMapsKey = "__maps";
