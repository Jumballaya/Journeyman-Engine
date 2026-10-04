#pragma once

#include <filesystem>
#include <string_view>

// OS-specific locations. Everything else in the engine stays path-agnostic.
namespace platform {

// The running executable (empty if unknown).
std::filesystem::path executablePath();
inline std::filesystem::path executableDir() { return executablePath().parent_path(); }

// Per-user save dir: ~/Library/Application Support/<game>, $XDG_DATA_HOME or
// ~/.local/share/<game>, or %APPDATA%/<game>; the temp dir as a last resort.
std::filesystem::path userDataDir(std::string_view gameName);

}  // namespace platform
