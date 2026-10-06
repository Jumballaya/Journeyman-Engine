#pragma once

#include <string>
#include <vector>

#include "Project.hpp"

// What the editor can tell about a script from its source, without compiling
// it: the params it reads, the callbacks it exports, and its opening comment.
struct ScriptInfo {
  struct Param {
    std::string key;
    bool number = true;
    Json fallback;  // the default it falls back to
  };
  std::vector<Param> params;
  std::vector<std::string> callbacks;  // "onUpdate", "onMessage", ...
  std::string description;             // the leading // comment, joined into lines
};

// Cached until the file changes.
const ScriptInfo& scriptInfo(const Project& project, const std::string& script);

// Scenes and prefabs whose entities run `script` (prefab instances count once, via the prefab).
std::vector<std::string> scriptUsers(const Project& project, const std::string& script);
