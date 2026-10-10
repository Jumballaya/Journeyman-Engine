#include "PlaySession.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <stdexcept>

#include "../logger/logging.hpp"
#include "Platform.hpp"

namespace session {
namespace {

constexpr uint64_t kSampleEvery = 30;  // 0.5 s at 60 fps
constexpr uint64_t kThumbEvery = 60;

std::string isoNow() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  return text;
}

// FNV-1a over the part's JSON: the same values, in the same order, give the
// same hash on every machine.
uint64_t partHash(const nlohmann::json& state, const char* part) {
  uint64_t hash = 1469598103934665603ull;
  const auto it = state.find(part);
  const std::string text = it == state.end() ? std::string() : it->dump();
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

std::filesystem::path newPlayDir(const std::filesystem::path& projectRoot) {
  const auto jm = projectRoot / ".jm";
  std::error_code ec;
  std::filesystem::create_directories(jm / "plays", ec);
  std::string error;  // unwritable: the Recorder reports the folder
  if (!std::filesystem::exists(jm / ".gitignore", ec)) platform::writeAtomically(jm / ".gitignore", "*\n", error);
  char stamp[32];
  const std::time_t now = std::time(nullptr);
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", std::localtime(&now));
  // Made here, atomically: another recorder starting this second (jm run, an
  // editor) gets the next name. An unwritable folder is the Recorder's to report.
  auto dir = jm / "plays" / stamp;
  for (int i = 2; !std::filesystem::create_directory(dir, ec) && !ec; ++i) {
    dir = jm / "plays" / (std::string(stamp) + "_" + std::to_string(i));
  }
  return dir;
}

uint64_t entitiesHash(const nlohmann::json& state) { return partHash(state, "entities"); }
uint64_t sessionHash(const nlohmann::json& state) { return partHash(state, "session"); }

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
  std::string error;
  if (!startingSave.empty() && !platform::writeAtomically(_dir / "save.json", startingSave, error)) {
    throw std::runtime_error(error);
  }
  if (!_frames || !_inputs || !_timeline) throw std::runtime_error("can't write a session in " + _dir.string());
  writeMeta();
}

Recorder::~Recorder() { end(); }

void Recorder::input(nlohmann::json event) {
  if (_ended) return;  // e.g. what the shutdown's last dispatch delivers
  event["f"] = _running.value_or(_framesRun);
  if (!_running) event["pre"] = true;
  _inputs << event.dump() << '\n';
}

void Recorder::frameStarts(uint64_t frame, bool focused) {
  _running = frame;
  if (focused == _focused) return;
  _focused = focused;
  input({{"type", "focus"}, {"focused", focused}});
}

std::optional<std::filesystem::path> Recorder::frameDone(uint64_t frame, float dt, const LazyState& state) {
  _frames.write(reinterpret_cast<const char*>(&dt), sizeof(dt));
  _framesRun = frame + 1;
  _running.reset();
  _seconds += dt;
  if (frame % kSampleEvery == 0) sample(frame, state());
  // Every second, the files are on disk: a crash loses at most that.
  if (_framesRun % 60 == 0) {
    _frames.flush();
    _inputs.flush();
    _timeline.flush();
    _meta["frames"] = _framesRun;
    _meta["seconds"] = _seconds;
    writeMeta();
  }
  if (frame % kThumbEvery != 0) return std::nullopt;
  char name[32];
  std::snprintf(name, sizeof(name), "%06llu.jpg", static_cast<unsigned long long>(frame));
  return _dir / "thumbs" / name;
}

void Recorder::sample(uint64_t frame, const nlohmann::json& state) {
  nlohmann::json line = {{"f", frame},
                         {"t", state.value("time", 0.0)},
                         {"scene", state.value("scene", std::string())},
                         {"entities", state.contains("entities") ? state["entities"].size() : 0},
                         {"session", state.value("session", nlohmann::json::object())},
                         {"hash", entitiesHash(state)},
                         {"sessionHash", sessionHash(state)}};
  // Mid-transition, the scene being left is still what's on screen.
  if (const auto t = state.find("transition"); t != state.end()) line["from"] = (*t).value("from", "");
  _timeline << line.dump() << '\n';
  _lastSample = frame;
}

Recorder::Marker Recorder::marker(uint64_t frame, double time, const nlohmann::json& state, const std::string& note) {
  const int n = static_cast<int>(_meta["markers"].size()) + 1;
  const std::string name = "markers/" + std::to_string(n);
  std::string error;
  if (!platform::writeAtomically(_dir / (name + ".json"), state.dump() + '\n', error)) JM_LOG_ERROR("[Recorder] {}", error);
  _meta["markers"].push_back({{"n", n},
                              {"frame", frame},
                              {"time", time},
                              {"scene", state.value("scene", std::string())},
                              {"image", name + ".png"}});
  if (!note.empty()) _meta["markers"].back()["note"] = note;
  writeMeta();
  return {n, _dir / (name + ".png")};
}

void Recorder::end(const nlohmann::json* last) {
  if (_ended) return;
  _ended = true;
  if (last && _framesRun > 0 && _lastSample != _framesRun - 1) sample(_framesRun - 1, *last);
  _frames.flush();
  _inputs.flush();
  _timeline.flush();
  _meta["frames"] = _framesRun;
  _meta["seconds"] = _seconds;
  _meta["ended"] = std::uncaught_exceptions() > 0 ? "crashed" : "quit";
  writeMeta();
}

void Recorder::writeMeta() {
  std::string error;  // whole or not at all: a reader never sees half a file
  if (!platform::writeAtomically(_dir / "session.json", _meta.dump(2) + '\n', error)) JM_LOG_ERROR("[Recorder] {}", error);
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
    else if (event.value("pre", false)) _before[frame].push_back(std::move(event));
    else _events[frame].push_back(std::move(event));
  }

  std::ifstream timeline(dir / "timeline.jsonl");
  for (std::string line; std::getline(timeline, line);) {
    auto sample = nlohmann::json::parse(line, nullptr, false);
    if (sample.is_discarded() || !sample.contains("f") || !sample.contains("hash")) continue;
    Hashes& hashes = _hashes[sample["f"].get<uint64_t>()];
    hashes.entities = sample["hash"].get<uint64_t>();
    if (sample.contains("sessionHash")) hashes.session = sample["sessionHash"].get<uint64_t>();
  }
}

namespace {

const std::vector<nlohmann::json>& eventsIn(const std::map<uint64_t, std::vector<nlohmann::json>>& events,
                                            uint64_t frame) {
  static const std::vector<nlohmann::json> none;
  const auto it = events.find(frame);
  return it == events.end() ? none : it->second;
}

}  // namespace

const std::vector<nlohmann::json>& Playback::eventsBefore(uint64_t frame) const { return eventsIn(_before, frame); }
const std::vector<nlohmann::json>& Playback::eventsAt(uint64_t frame) const { return eventsIn(_events, frame); }

std::optional<bool> Playback::matchesAt(uint64_t frame, const LazyState& state) const {
  const auto it = _hashes.find(frame);
  if (it == _hashes.end()) return std::nullopt;
  const Hashes& recorded = it->second;
  return recorded.entities == entitiesHash(state()) && (!recorded.session || *recorded.session == sessionHash(state()));
}

bool Playback::focusedAt(uint64_t frame) const {
  // The last change at or before `frame`; focused before any.
  auto it = _focus.upper_bound(frame);
  if (it == _focus.begin()) return true;
  return std::prev(it)->second;
}

}  // namespace session
