#pragma once

#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// A thread-safe key/value store of numbers and strings that outlives scenes.
// Scripts use one instance for session state (score, lives, current stage)
// and a second, file-backed instance for data that survives restarts (high
// score, settings).
//
// When constructed with a file path, the store loads it immediately and
// writes it back on flush() if anything changed. Without a path it is purely
// in-memory.
class GameState {
 public:
  GameState() = default;
  explicit GameState(std::filesystem::path file);

  void setNumber(const std::string& key, double value);
  double getNumber(const std::string& key, double fallback) const;
  void setString(const std::string& key, std::string value);
  std::optional<std::string> getString(const std::string& key) const;
  bool has(const std::string& key) const;
  void remove(const std::string& key);
  void clear();

  // Writes the file if dirty. Main thread (called once per frame + shutdown).
  void flush();

 private:
  mutable std::mutex _mutex;
  nlohmann::json _values = nlohmann::json::object();
  std::filesystem::path _file;
  bool _dirty = false;
};
