#pragma once

#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

// Thread-safe numbers and strings that outlive scenes. With a file path it loads
// it now and flush() writes changes back; without one it is memory only.
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
