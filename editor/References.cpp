#include "References.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <regex>

namespace {

// Text files that can name other files.
bool canRefer(AssetKind kind) {
  switch (kind) {
    case AssetKind::Scene:
    case AssetKind::Prefab:
    case AssetKind::Script:
    case AssetKind::Atlas:
    case AssetKind::Tileset:
    case AssetKind::Ui:
    case AssetKind::Style:
    case AssetKind::Data:
    case AssetKind::Input: return true;
    default: return false;
  }
}

// How a script names the file by its short name ("coin" for coin.prefab.json)
// in the call that loads it, or nothing when that kind isn't loaded that way.
std::optional<std::regex> shortNameUse(const std::string& path) {
  std::string escaped;
  for (char c : assetStem(path)) escaped += std::isalnum(static_cast<unsigned char>(c)) ? std::string(1, c) : "\\" + std::string(1, c);
  const std::string quoted = "\"" + escaped + "\"";
  switch (assetKindOf(path)) {
    case AssetKind::Prefab: return std::regex("spawn\\s*\\(\\s*" + quoted);
    case AssetKind::Scene: return std::regex("Scene\\.\\w+\\(\\s*" + quoted);
    case AssetKind::Sound: return std::regex("(Sound|Music)\\s*\\(\\s*" + quoted);
    case AssetKind::Shader: return std::regex("(PostEffect\\.\\w+\\(\\s*" + quoted + "|transition\\([^)]*" + quoted + ")");
    default: return std::nullopt;
  }
}

}  // namespace

std::vector<std::string> referencesTo(const Project& project, const std::string& path) {
  static std::map<std::string, std::pair<size_t, std::vector<std::string>>> cache;
  size_t signature = project.files().size();
  for (const AssetFile& f : project.files()) signature = signature * 31 + static_cast<size_t>(f.modified.time_since_epoch().count());
  auto& entry = cache[path];
  if (entry.first == signature && signature != 0) return entry.second;
  std::vector<std::string> out;
  const std::string quotedPath = "\"" + path + "\"";
  const std::string inUrl = "(" + path + ")";
  const auto byShortName = shortNameUse(path);
  for (const AssetFile& f : project.files()) {
    if (f.path == path || !canRefer(f.kind) || f.path.find("node_modules") != std::string::npos) continue;
    const std::string text = project.readText(f.path);
    const bool byPath = text.find(quotedPath) != std::string::npos || text.find(inUrl) != std::string::npos ||
                        text.find("\"" + path + "#") != std::string::npos;
    const bool byName = byShortName && f.kind == AssetKind::Script && std::regex_search(text, *byShortName);
    if (byPath || byName) out.push_back(f.path);
  }
  entry = {signature, out};
  return out;
}
