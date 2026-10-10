#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// OS-specific locations and file replacement. Everything else stays path-agnostic.
namespace platform {

// The running executable (empty if unknown).
std::filesystem::path executablePath();
inline std::filesystem::path executableDir() { return executablePath().parent_path(); }

// Per-user save dir: ~/Library/Application Support/<game>, $XDG_DATA_HOME or
// ~/.local/share/<game>, or %APPDATA%/<game>; the temp dir as a last resort.
std::filesystem::path userDataDir(std::string_view gameName);

// Writes beside `target`, then renames over it: a crash or a full disk never
// leaves half a file. Makes missing folders. False (with `error`) on failure.
bool writeAtomically(const std::filesystem::path& target, std::string_view bytes, std::string& error);

}  // namespace platform
