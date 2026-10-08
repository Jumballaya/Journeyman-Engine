#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Project.hpp"

// A project file open in an asset editor (a tileset, input bindings, an
// atlas, a data table, a shader...), with its undo history. JSON files are
// edited as their parsed value; anything else as one string. Edits save
// themselves once they settle (see Editor), so there is never a save prompt;
// a change made on disk by another program reloads the document.
class AssetDocument {
 public:
  // Null with `error` set if the file can't be read (or isn't valid JSON).
  static std::unique_ptr<AssetDocument> load(const Project& project, std::string path, std::string& error);

  const std::string& path() const { return _path; }
  // The file was moved (by the editor): the document follows it, edits and all.
  void movedTo(std::string path) { _path = std::move(path); }
  std::string title() const;  // the file name
  bool isText() const { return _value.is_string(); }
  const Json& value() const { return _value; }
  const std::string& text() const { return _value.get_ref<const std::string&>(); }

  // One undoable change ("Add Action"). Consecutive edits sharing a non-empty
  // `mergeKey` fold into one step (see gestureKey).
  void edit(const std::string& label, const std::function<void(Json& value)>& mutate, const std::string& mergeKey = {});
  void setText(const std::string& text, const std::string& label, const std::string& mergeKey = {});

  bool canUndo() const { return _cursor > 0; }
  bool canRedo() const { return _cursor < _history.size(); }
  std::string undoLabel() const { return canUndo() ? _history[_cursor - 1].label : ""; }
  std::string redoLabel() const { return canRedo() ? _history[_cursor].label : ""; }
  void undo();
  void redo();

  bool dirty() const { return _value != _saved; }
  // Seconds (ImGui time) of the last change; autosave waits for it to settle.
  double changedAt() const { return _changedAt; }
  bool save(const Project& project, std::string& error);
  // Another program changed the file (an agent's edit): its version becomes an
  // undoable step ("Change on Disk") that counts as saved, so unsaved edits
  // here stay one Undo away. What happened:
  enum class DiskChange { None, Reloaded, ReloadedOverEdits };
  DiskChange reloadIfChanged(const Project& project);
  // Bumps on every change, so views can cache what they derive.
  uint64_t revision() const { return _revision; }

 private:
  struct Step {
    std::string label;
    Json before, after;
    std::string mergeKey;
  };
  std::string _path;
  Json _value, _saved;
  bool _endsWithNewline = true;
  std::vector<Step> _history;
  size_t _cursor = 0;
  uint64_t _revision = 0;
  double _changedAt = 0;
  std::filesystem::file_time_type _diskTime{};

  std::string serialized() const;
  void changed();
};
