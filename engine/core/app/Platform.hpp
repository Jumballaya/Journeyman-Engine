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
// leaves half a file. Makes missing folders; keeps a symlink and the file's mode.
// False (with `error`) on failure.
bool writeAtomically(const std::filesystem::path& target, std::string_view bytes, std::string& error);

// Windows builds the engine and editor as GUI programs, so no console window
// opens with them. Run from a terminal, this sends their output back to it;
// redirected (jm pipes it) or started from the Start menu, it does nothing.
// A no-op elsewhere.
void attachParentConsole();

}  // namespace platform
