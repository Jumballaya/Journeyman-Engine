#include "PlaySession.hpp"

#include <chrono>
#include <ctime>
#include <stdexcept>

namespace session {
namespace {

std::string isoNow() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return text;
}

}  // namespace

uint64_t entitiesHash(const nlohmann::json& state) {
  // FNV-1a over the entities' JSON: the same entities, ids and values, in the
  // same order, give the same hash on every machine.
  uint64_t hash = 1469598103934665603ull;
  const auto it = state.find("entities");
  const std::string text = it == state.end() ? std::string() : it->dump();
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

Recorder::Recorder(std::filesystem::path dir, nlohmann::json meta, const std::string& startingSave)
    : _dir(std::move(dir)), _meta(std::move(meta)) {
  std::filesystem::create_directories(_dir / "thumbs");
  std::filesystem::create_directories(_dir / "markers");
  _meta["format"] = kFormat;
  _meta["started"] = isoNow();
  _meta["frames"] = 0;
  _meta["seconds"] = 0.0;
  _meta["markers"] = nlohmann::json::array();
  _meta["ended"] = "running";  // until end(): a crash leaves it so
  _frames.open(_dir / "frames.bin", std::ios::binary | std::ios::trunc);
  _inputs.open(_dir / "inputs.jsonl", std::ios::trunc);
  _timeline.open(_dir / "timeline.jsonl", std::ios::trunc);
  if (!startingSave.empty()) std::ofstream(_dir / "save.json") << startingSave;
  if (!_frames || !_inputs || !_timeline) throw std::runtime_error("can't write a session in " + _dir.string());
  writeMeta();
}

Recorder::~Recorder() { end("quit"); }

void Recorder::input(uint64_t frame, nlohmann::json event) {
  event["f"] = frame;
  _inputs << event.dump() << '\n';
}

void Recorder::frameDone(uint64_t frame, float dt, const nlohmann::json* state) {
  _frames.write(reinterpret_cast<const char*>(&dt), sizeof(dt));
  _framesRun = frame + 1;
  _seconds += dt;
  if (state) sample(frame, *state);
  // Every second, the files are on disk: a crash loses at most that.
  if (_framesRun % 60 == 0) {
    _frames.flush();
    _inputs.flush();
    _timeline.flush();
    _meta["frames"] = _framesRun;
    _meta["seconds"] = _seconds;
    writeMeta();
  }
}

void Recorder::sample(uint64_t frame, const nlohmann::json& state) {
  nlohmann::json line = {{"f", frame},
                         {"t", state.value("time", 0.0)},
                         {"scene", state.value("scene", std::string())},
                         {"entities", state.contains("entities") ? state["entities"].size() : 0},
                         {"session", state.value("session", nlohmann::json::object())},
                         {"hash", entitiesHash(state)}};
  // Mid-transition, the scene being left is still what's on screen.
  if (const auto t = state.find("transition"); t != state.end()) line["from"] = (*t).value("from", "");
  _timeline << line.dump() << '\n';
  _lastSample = frame;
}

int Recorder::marker(uint64_t frame, double time, const nlohmann::json& state, const std::string& note) {
  const int n = static_cast<int>(_meta["markers"].size()) + 1;
  std::ofstream(_dir / "markers" / (std::to_string(n) + ".json")) << state.dump() << '\n';
  _meta["markers"].push_back({{"n", n},
                              {"frame", frame},
                              {"time", time},
                              {"scene", state.value("scene", std::string())},
                              {"image", "markers/" + std::to_string(n) + ".png"}});
  if (!note.empty()) _meta["markers"].back()["note"] = note;
  writeMeta();
  return n;
}

void Recorder::end(const std::string& how, const nlohmann::json* last) {
  if (_ended) return;
  _ended = true;
  if (last && _framesRun > 0 && _lastSample != _framesRun - 1) sample(_framesRun - 1, *last);
  _frames.flush();
  _inputs.flush();
  _timeline.flush();
  _meta["frames"] = _framesRun;
  _meta["seconds"] = _seconds;
  _meta["ended"] = how;
  writeMeta();
}

void Recorder::writeMeta() {
  // Whole or not at all: a reader never sees half a file.
  const auto tmp = _dir / "session.json.tmp";
  std::ofstream(tmp) << _meta.dump(2) << '\n';
  std::error_code ec;
  std::filesystem::rename(tmp, _dir / "session.json", ec);
}

Playback::Playback(const std::filesystem::path& dir) {
  std::ifstream metaIn(dir / "session.json");
  if (!metaIn) throw std::runtime_error("no session at " + dir.string() + " (no session.json)");
  _meta = nlohmann::json::parse(metaIn, nullptr, false);
  if (_meta.is_discarded()) throw std::runtime_error(dir.string() + "/session.json isn't valid JSON");
  if (_meta.value("format", 0) > kFormat) {
    throw std::runtime_error("session " + dir.string() + " is a newer format than this engine reads");
  }

  std::ifstream frames(dir / "frames.bin", std::ios::binary);
  for (float dt; frames.read(reinterpret_cast<char*>(&dt), sizeof(dt));) _dts.push_back(dt);

  std::ifstream inputs(dir / "inputs.jsonl");
  for (std::string line; std::getline(inputs, line);) {
    auto event = nlohmann::json::parse(line, nullptr, false);
    if (event.is_discarded() || !event.contains("f")) continue;  // a crash's last, partial line
    const uint64_t frame = event["f"].get<uint64_t>();
    if (event.value("type", "") == "focus") _focus[frame] = event.value("focused", true);
    else _events[frame].push_back(std::move(event));
  }

  std::ifstream timeline(dir / "timeline.jsonl");
  for (std::string line; std::getline(timeline, line);) {
    auto sample = nlohmann::json::parse(line, nullptr, false);
    if (!sample.is_discarded() && sample.contains("f") && sample.contains("hash")) {
      _hashes[sample["f"].get<uint64_t>()] = sample["hash"].get<uint64_t>();
    }
  }
}

const std::vector<nlohmann::json>& Playback::eventsAt(uint64_t frame) const {
  static const std::vector<nlohmann::json> none;
  const auto it = _events.find(frame);
  return it == _events.end() ? none : it->second;
}

std::optional<uint64_t> Playback::hashAt(uint64_t frame) const {
  const auto it = _hashes.find(frame);
  if (it == _hashes.end()) return std::nullopt;
  return it->second;
}

bool Playback::focusedAt(uint64_t frame) const {
  // The last change at or before `frame`; focused before any.
  auto it = _focus.upper_bound(frame);
  if (it == _focus.begin()) return true;
  return std::prev(it)->second;
}

}  // namespace session
