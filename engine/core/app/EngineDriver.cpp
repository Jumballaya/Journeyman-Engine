// The stepped driver (JM_DRIVE=1): a tool runs the game a command at a time.
// Each command is a line on stdin; each gets one JSON line on stdout. The game
// advances only on "step", by the fixed dt, so the tool can take as long as it
// likes between steps and the run stays reproducible (JM_DRIVE_RECORD writes
// its inputs as a replay).
//
//   step [n] [dt]       run n frames (default 1), each dt seconds (default the
//                       fixed step; a session replay's own while it lasts)
//                       -> {"ok", "frame", "errors"}
//   state [part...]     the state dump (stateJson) -> {"ok", "state"}; parts
//                       pick its keys (entities, session, ui, draw, ...),
//                       tag=Name keeps the entities with that tag, and a
//                       component name only that component
//   get [tag=Name] path one value: get session.score, get tag=Ball
//                       TransformComponent.x -> {"ok", "value"}
//   set <key> <json>    a session value (GameState), as scripts' State.set
//   scene <path>        load a scene (on the next step)
//   down|up|press <Key> a key, from the next frame (inputs module)
//   move x y, click [x y] [button], mousedown|mouseup [x y] [button], wheel dy
//                       the mouse, in logical px (renderer module)
//   marker [note]       a marker in the recorded session (JM_RECORD_DIR)
//   capture <path>      the last frame as a PNG (renderer module)
//   quit
//
// A command that fails answers {"ok": false, "error": "..."}; the run goes on.

#include <algorithm>
#include <cctype>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "Engine.hpp"

namespace {

constexpr const char* kCommands = "step [n] [dt], marker [note], move x y, click [x y], wheel dy, state [part...] [tag=Name...] [Component...], get [tag=Name] <path>, set <key> <json>, scene <path>, down|up|press <Key>, capture <path>, quit";

nlohmann::json failure(std::string message) { return {{"ok", false}, {"error", std::move(message)}}; }

std::vector<std::string> words(std::string_view args) {
  std::vector<std::string> out;
  for (size_t at = args.find_first_not_of(" \t"); at < args.size();) {
    const size_t end = std::min(args.find_first_of(" \t", at), args.size());
    out.emplace_back(args.substr(at, end - at));
    at = std::min(args.find_first_not_of(" \t", end), args.size());
  }
  return out;
}

// The parts of `state` that `state <part>...` asked for: its named keys (frame
// always); `tag=Name` keeps the entities with that tag and a component name
// (`TransformComponent`) only that component of each. "" or an error.
std::string selectState(nlohmann::json& state, std::string_view args) {
  std::vector<std::string> keys, tags, components;
  for (const std::string& word : words(args)) {
    if (word.starts_with("tag=")) {
      tags.push_back(word.substr(4));
    } else if (word.ends_with("Component")) {
      components.push_back(word);
    } else if (state.contains(word)) {
      keys.push_back(word);
    } else {
      std::string known;
      for (const auto& [key, value] : state.items()) known += (known.empty() ? "" : ", ") + key;
      return "state has no '" + word + "' (parts: " + known +
             "; tag=Name for entities with a tag, a component name for just that component)";
    }
  }
  if (!tags.empty() || !components.empty()) {
    nlohmann::json kept = nlohmann::json::array();
    for (nlohmann::json& entity : state["entities"]) {
      const nlohmann::json& names = entity["tags"];
      const bool tagged = tags.empty() || std::ranges::any_of(tags, [&](const std::string& tag) {
        return std::find(names.begin(), names.end(), tag) != names.end();
      });
      if (!tagged) continue;
      if (!components.empty()) {
        nlohmann::json only = nlohmann::json::object();
        for (const std::string& c : components) {
          if (entity["components"].contains(c)) only[c] = std::move(entity["components"][c]);
        }
        if (only.empty()) continue;  // has none of them
        entity["components"] = std::move(only);
      }
      kept.push_back(std::move(entity));
    }
    state["entities"] = std::move(kept);
    if (std::ranges::find(keys, "entities") == keys.end()) keys.push_back("entities");
  }
  if (keys.empty()) return "";
  nlohmann::json picked = {{"frame", state["frame"]}};
  for (const std::string& key : keys) picked[key] = std::move(state[key]);
  state = std::move(picked);
  return "";
}

// `get [tag=Name] <path>`: one value from the state. With a tag, the path is
// into that entity's components (TransformComponent.x); without, into the
// state (session.score, scene). Several tagged entities give "values".
nlohmann::json getValue(const nlohmann::json& state, std::string_view args) {
  std::string tag, path;
  for (const std::string& word : words(args)) {
    std::string& slot = word.starts_with("tag=") ? tag : path;
    if (!slot.empty()) return failure("get takes one path (and a tag=Name), e.g. get tag=Ball TransformComponent.x");
    slot = word.starts_with("tag=") ? word.substr(4) : word;
  }
  if (path.empty()) return failure("get takes a path, e.g. get session.score, or get tag=Ball TransformComponent.x");
  auto lookup = [&](const nlohmann::json& from) -> const nlohmann::json* {
    const nlohmann::json* at = &from;
    for (size_t start = 0; start <= path.size();) {
      const size_t dot = std::min(path.find('.', start), path.size());
      const std::string part = path.substr(start, dot - start);
      start = dot + 1;
      if (at->is_object() && at->contains(part)) {
        at = &(*at)[part];
      } else if (at->is_array() && !part.empty() && part.size() <= 9 && std::ranges::all_of(part, ::isdigit) &&
                 std::stoul(part) < at->size()) {  // <= 9 digits: stoul can't overflow
        at = &(*at)[std::stoul(part)];
      } else {
        return nullptr;
      }
    }
    return at;
  };
  if (tag.empty()) {
    const nlohmann::json* v = lookup(state);
    return v ? nlohmann::json{{"ok", true}, {"value", *v}} : failure("state has no " + path);
  }
  nlohmann::json values = nlohmann::json::array();
  for (const nlohmann::json& entity : state["entities"]) {
    const nlohmann::json& names = entity["tags"];
    if (std::find(names.begin(), names.end(), tag) == names.end()) continue;
    if (const nlohmann::json* v = lookup(entity["components"])) values.push_back(*v);
  }
  if (values.empty()) return failure("no entity tagged " + tag + " has " + path);
  if (values.size() == 1) return {{"ok", true}, {"value", values[0]}};
  return {{"ok", true}, {"values", values}};
}

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
      float stepSeconds = dt;
      const std::vector<std::string> w = words(args);
      try {
        if (!w.empty()) n = std::stoll(w[0]);
        if (w.size() > 1) stepSeconds = std::stof(w[1]);
      } catch (const std::exception&) {
        n = -1;
      }
      if (n < 0 || w.size() > 2 || !(stepSeconds > 0.0f)) {
        reply(failure("step takes a frame count and a dt in seconds, e.g. step 60, or step 10 0.021"));
        continue;
      }
      for (long long i = 0; i < n && _running; ++i) frame(stepSeconds);
      nlohmann::json message = {{"ok", true}, {"frame", _frames}};
      if (!_running) message["quit"] = true;  // the game asked to quit
      reply(withErrors(std::move(message)));
      continue;
    }
    if (verb == "state") {
      nlohmann::json state = stateJson();
      if (std::string error = selectState(state, args); !error.empty()) {
        reply(failure(std::move(error)));
        continue;
      }
      reply(withErrors({{"ok", true}, {"state", std::move(state)}}));
      continue;
    }
    if (verb == "get") {
      reply(getValue(stateJson(), args));
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
      giveInput({{"type", "set"}, {"key", args.substr(0, space)}, {"value", value}});
      reply({{"ok", true}});
      continue;
    }
    if (verb == "marker") {
      const int n = dropMarker(args);
      reply(n > 0 ? nlohmann::json{{"ok", true}, {"marker", n}}
                  : failure("this run isn't recorded (JM_RECORD_DIR): nothing to mark"));
      continue;
    }
    if (verb == "scene") {
      if (args.empty()) {
        reply(failure("scene takes a path, e.g. scene scenes/level2.scene.json"));
        continue;
      }
      giveInput({{"type", "scene"}, {"path", args}});
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
