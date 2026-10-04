#pragma once

#include <filesystem>
#include <string_view>

// OS-specific locations. Everything else in the engine stays path-agnostic.
namespace platform {

// Directory containing the running executable (empty if unknown).
std::filesystem::path executableDir();

// Per-user writable directory for a game's saves:
//   macOS   ~/Library/Application Support/<game>
//   Linux   $XDG_DATA_HOME/<game> or ~/.local/share/<game>
//   Windows %APPDATA%/<game>
// Falls back to the temp directory when no home is known.
std::filesystem::path userDataDir(std::string_view gameName);

}  // namespace platform
