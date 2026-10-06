#pragma once

#include <string>
#include <vector>

#include "Project.hpp"

// The project files that refer to `path`: by its path anywhere (scenes,
// prefabs, tilesets, UI, stylesheets, data, the manifest), or by its short
// name where scripts use one (spawn("coin"), new Sound("jump"),
// Scene.load("level2"), shader names). Cached until any file changes.
std::vector<std::string> referencesTo(const Project& project, const std::string& path);
