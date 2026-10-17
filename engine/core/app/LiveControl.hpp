#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

class Engine;

// A game the person plays, steered from outside while it runs (JM_CONTROL=dir;
// jm mcp's play_live). Requests are lines appended to dir/in, each
// {"id", "command"}: a driver command (Engine::command), or "pause", "resume",
// "speed <0.1..1>", "status". Each answer is written to dir/answers/<id>.json.
// dir/status.json says the run is live (frame, scene, paused, speed, updated),
// rewritten on change and every second, and when it started (started: Unix ms,
// the run's identity). The folder is the run's: removed when it ends.
class LiveControl {
 public:
  explicit LiveControl(std::filesystem::path dir);  // clears what an earlier run left
  ~LiveControl();
  LiveControl(const LiveControl&) = delete;
  LiveControl& operator=(const LiveControl&) = delete;

  // Between frames: runs the requests that came in, keeps status.json current.
  void poll(Engine& engine);
  // How fast game time runs: 0 while paused.
  float timeScale() const { return _paused ? 0.0f : _speed; }

 private:
  std::filesystem::path _dir;
  std::uintmax_t _read = 0;  // bytes of dir/in taken
  bool _paused = false;
  float _speed = 1.0f;
  nlohmann::json _status;
  long long _started;  // Unix ms
  std::chrono::steady_clock::time_point _statusAt{};

  nlohmann::json run(Engine& engine, const std::string& command);
  nlohmann::json status(Engine& engine) const;
};
