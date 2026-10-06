#include "GameState.hpp"

#include <fstream>

#include "../logger/logging.hpp"

GameState::GameState(std::filesystem::path file) : _file(std::move(file)) {
  std::ifstream in(_file);
  if (!in) return;
  try {
    nlohmann::json loaded = nlohmann::json::parse(in);
    if (loaded.is_object()) _values = std::move(loaded);
  } catch (const std::exception& e) {
    JM_LOG_WARN("[GameState] ignoring unreadable save file '{}': {}", _file.string(), e.what());
  }
}

void GameState::setNumber(const std::string& key, double value) { setJson(key, value); }

double GameState::getNumber(const std::string& key, double fallback) const {
  const auto value = getJson(key);
  return value && value->is_number() ? value->get<double>() : fallback;
}

void GameState::setString(const std::string& key, std::string value) { setJson(key, std::move(value)); }

std::optional<std::string> GameState::getString(const std::string& key) const {
  const auto value = getJson(key);
  if (!value || !value->is_string()) return std::nullopt;
  return value->get<std::string>();
}

bool GameState::has(const std::string& key) const {
  std::lock_guard lock(_mutex);
  return _values.contains(key);
}

void GameState::remove(const std::string& key) {
  std::lock_guard lock(_mutex);
  if (_values.erase(key) > 0) _dirty = true;
}

void GameState::clear() {
  std::lock_guard lock(_mutex);
  if (!_values.empty()) _dirty = true;
  _values = nlohmann::json::object();
}

void GameState::setJson(const std::string& key, nlohmann::json value) {
  std::lock_guard lock(_mutex);
  auto it = _values.find(key);
  if (it != _values.end() && *it == value) return;
  _values[key] = std::move(value);
  _dirty = true;
}

std::optional<nlohmann::json> GameState::getJson(const std::string& key) const {
  std::lock_guard lock(_mutex);
  auto it = _values.find(key);
  if (it == _values.end()) return std::nullopt;
  return *it;
}

std::vector<std::string> GameState::keys(std::string_view prefix) const {
  std::lock_guard lock(_mutex);
  std::vector<std::string> out;
  for (const auto& [key, _] : _values.items()) {  // json objects iterate sorted
    if (std::string_view(key).starts_with(prefix)) out.push_back(key);
  }
  return out;
}

void GameState::flush() {
  std::string text;
  {
    std::lock_guard lock(_mutex);
    if (!_dirty || _file.empty()) return;
    text = _values.dump(2);
    _dirty = false;
  }
  std::error_code ec;
  std::filesystem::create_directories(_file.parent_path(), ec);
  // Write-then-rename so a crash mid-write never corrupts the save.
  const auto tmp = std::filesystem::path(_file).concat(".tmp");
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      JM_LOG_ERROR("[GameState] cannot write '{}'", tmp.string());
      return;
    }
    out << text;
  }
  std::filesystem::rename(tmp, _file, ec);
  if (ec) JM_LOG_ERROR("[GameState] cannot replace '{}': {}", _file.string(), ec.message());
}
