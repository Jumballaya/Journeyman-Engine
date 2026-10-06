#include "ScriptInfo.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <regex>
#include <sstream>

#include "References.hpp"

namespace {

ScriptInfo read(const std::string& source) {
  ScriptInfo info;
  static const std::regex param(R"re(Params\.(number|text)\(\s*"([A-Za-z0-9_]+)"\s*(?:,\s*([^),]+))?)re");
  for (auto it = std::sregex_iterator(source.begin(), source.end(), param); it != std::sregex_iterator(); ++it) {
    const std::string key = (*it)[2];
    if (std::any_of(info.params.begin(), info.params.end(), [&](const ScriptInfo::Param& p) { return p.key == key; })) continue;
    ScriptInfo::Param p{key, (*it)[1] == "number", nullptr};
    const std::string fallback = (*it)[3].matched ? std::string((*it)[3]) : std::string();
    if (p.number) {
      char* end = nullptr;
      const double v = std::strtod(fallback.c_str(), &end);
      p.fallback = fallback.empty() || end == fallback.c_str() ? Json(0) : Json(v);
    } else {
      p.fallback = fallback.size() >= 2 && fallback.front() == '"' ? Json(fallback.substr(1, fallback.size() - 2)) : Json("");
    }
    info.params.push_back(std::move(p));
  }
  static const std::regex callback(R"(export\s+function\s+(on[A-Z]\w*)\s*\()");
  for (auto it = std::sregex_iterator(source.begin(), source.end(), callback); it != std::sregex_iterator(); ++it) {
    info.callbacks.push_back((*it)[1]);
  }
  // The comment block the file opens with (before any code).
  std::istringstream lines(source);
  for (std::string line; std::getline(lines, line);) {
    line.erase(0, line.find_first_not_of(" \t"));
    if (!line.starts_with("//")) break;
    line.erase(0, line.starts_with("// ") ? 3 : 2);
    info.description += (info.description.empty() ? "" : " ") + line;
  }
  return info;
}

}  // namespace

const ScriptInfo& scriptInfo(const Project& project, const std::string& script) {
  static std::map<std::string, std::pair<std::filesystem::file_time_type, ScriptInfo>> cache;
  std::error_code ec;
  const auto modified = std::filesystem::last_write_time(project.abs(script), ec);
  auto& entry = cache[script];
  if (!ec && entry.first != modified) entry = {modified, read(project.readText(script))};
  return entry.second;
}

std::vector<std::string> scriptUsers(const Project& project, const std::string& script) {
  std::vector<std::string> users = referencesTo(project, script);
  std::erase_if(users, [](const std::string& path) {
    const AssetKind kind = assetKindOf(path);
    return kind != AssetKind::Scene && kind != AssetKind::Prefab;
  });
  return users;
}
