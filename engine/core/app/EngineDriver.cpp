// The stepped driver (JM_DRIVE=1): a tool runs the game a command at a time.
// Each command is a line on stdin; each gets one JSON line on stdout. The game
// advances only on "step", by the fixed dt, so the tool can take as long as it
// likes between steps and the run stays reproducible (JM_DRIVE_RECORD writes
// its inputs as a replay).
//
//   step [n]            run n frames (default 1)  -> {"ok", "frame", "errors"}
//   state               the state dump (stateJson) -> {"ok", "state"}
//   set <key> <json>    a session value (GameState), as scripts' State.set
//   scene <path>        load a scene (on the next step)
//   down|up|press <Key> a key, from the next frame (inputs module)
//   capture <path>      the last frame as a PNG (renderer module)
//   quit
//
// A command that fails answers {"ok": false, "error": "..."}; the run goes on.

#include <istream>
#include <ostream>
#include <string>

#include "Engine.hpp"

namespace {

constexpr const char* kCommands = "step [n], state, set <key> <json>, scene <path>, down|up|press <Key>, capture <path>, quit";

nlohmann::json failure(std::string message) { return {{"ok", false}, {"error", std::move(message)}}; }

}  // namespace

void Engine::drive(std::istream& in, std::ostream& out) {
  auto reply = [&](const nlohmann::json& message) { out << message.dump() << std::endl; };
  auto withErrors = [this](nlohmann::json message) {
    message["errors"] = std::move(_driverErrors);
    _driverErrors.clear();
    return message;
  };
  const float dt = _options.dev.fixedDt > 0.0f ? _options.dev.fixedDt : 1.0f / 60.0f;
  reply(withErrors({{"ok", true}, {"ready", true}, {"frame", _frames}, {"scene", _sceneManager.getCurrentScenePath()}}));

  for (std::string line; _running && std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') continue;
    const size_t verbEnd = line.find_first_of(" \t", start);
    const std::string verb = line.substr(start, verbEnd == std::string::npos ? std::string::npos : verbEnd - start);
    std::string args = verbEnd == std::string::npos ? std::string() : line.substr(verbEnd);
    args.erase(0, args.find_first_not_of(" \t"));
    args.erase(args.find_last_not_of(" \t") + 1);

    if (verb == "quit") {
      reply({{"ok", true}});
      break;
    }
    if (verb == "step") {
      long long n = 1;
      if (!args.empty()) {
        try {
          n = std::stoll(args);
        } catch (const std::exception&) {
          n = -1;
        }
      }
      if (n < 0) {
        reply(failure("step takes a frame count, e.g. step 60"));
        continue;
      }
      for (long long i = 0; i < n && _running; ++i) frame(dt);
      nlohmann::json message = {{"ok", true}, {"frame", _frames}};
      if (!_running) message["quit"] = true;  // the game asked to quit
      reply(withErrors(std::move(message)));
      continue;
    }
    if (verb == "state") {
      reply(withErrors({{"ok", true}, {"state", stateJson()}}));
      continue;
    }
    if (verb == "set") {
      const size_t space = args.find_first_of(" \t");
      const nlohmann::json value =
          space == std::string::npos ? nlohmann::json() : nlohmann::json::parse(args.substr(space), nullptr, false);
      if (space == std::string::npos || value.is_discarded()) {
        reply(failure("set takes a key and a JSON value, e.g. set lives 3"));
        continue;
      }
      _session.setJson(args.substr(0, space), value);
      reply({{"ok", true}});
      continue;
    }
    if (verb == "scene") {
      if (args.empty()) {
        reply(failure("scene takes a path, e.g. scene scenes/level2.scene.json"));
        continue;
      }
      _sceneManager.loadScene(args);
      reply({{"ok", true}});
      continue;
    }
    nlohmann::json message;
    if (_modules.driveCommand(*this, verb, args, message)) {
      reply(message);
    } else {
      reply(failure("unknown command '" + verb + "' (commands: " + kCommands + ")"));
    }
  }
}
