#include "LiveControl.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "Engine.hpp"
#include "Platform.hpp"
#include "../logger/logging.hpp"

namespace fs = std::filesystem;

LiveControl::LiveControl(fs::path dir) : _dir(std::move(dir)) {
  std::error_code ec;
  fs::remove_all(_dir, ec);
  fs::create_directories(_dir / "answers", ec);
  std::ofstream(_dir / "in", std::ios::trunc);
}

LiveControl::~LiveControl() {
  std::error_code ec;
  fs::remove(_dir / "status.json", ec);
}

void LiveControl::poll(Engine& engine) {
  std::error_code ec;
  if (const std::uintmax_t size = fs::file_size(_dir / "in", ec); !ec && size > _read) {
    std::ifstream in(_dir / "in", std::ios::binary);
    in.seekg(static_cast<std::streamoff>(_read));
    const std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t end = chunk.rfind('\n');  // a line still being written waits
    if (end != std::string::npos) {
      _read += end + 1;
      for (size_t from = 0, nl = chunk.find('\n'); from <= end; from = nl + 1, nl = chunk.find('\n', from)) {
        const nlohmann::json request = nlohmann::json::parse(chunk.substr(from, nl - from), nullptr, false);
        if (!request.is_object() || !request.contains("id") || !request["id"].is_string()) continue;
        const std::string id = request["id"];
        if (id.empty() || id.find_first_of("/\\.") != std::string::npos) continue;  // a file name
        nlohmann::json answer = run(engine, request.value("command", ""));
        std::string error;
        if (!platform::writeAtomically(_dir / "answers" / (id + ".json"), answer.dump(), error)) JM_LOG_WARN("[Engine] JM_CONTROL: {}", error);
      }
    }
  }
  nlohmann::json now = status(engine);
  const auto at = std::chrono::steady_clock::now();
  nlohmann::json changes = now;
  changes.erase("frame");  // frames tick by: they wait for the heartbeat
  if (changes == _status && at - _statusAt < std::chrono::seconds(1)) return;
  _status = std::move(changes);
  _statusAt = at;
  nlohmann::json file = now;
  file["updated"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::string error;
  platform::writeAtomically(_dir / "status.json", file.dump(), error);
}

nlohmann::json LiveControl::run(Engine& engine, const std::string& command) {
  if (command == "pause" || command == "resume") {
    _paused = command == "pause";
    return status(engine);
  }
  if (command.starts_with("speed")) {
    const float speed = std::strtof(command.c_str() + 5, nullptr);
    if (!(speed > 0.0f)) return {{"ok", false}, {"error", "speed takes how fast, 0.1 to 1 (1: full speed)"}};
    _speed = std::clamp(speed, 0.1f, 1.0f);
    return status(engine);
  }
  if (command == "status") return status(engine);
  nlohmann::json answer = engine.command(command);
  return answer.is_null() ? nlohmann::json{{"ok", false}, {"error", "no command"}} : answer;
}

nlohmann::json LiveControl::status(Engine& engine) const {
  return {{"ok", true}, {"frame", engine.frameCount()}, {"scene", engine.getSceneManager().getCurrentScenePath()},
          {"paused", _paused}, {"speed", _speed}};
}
