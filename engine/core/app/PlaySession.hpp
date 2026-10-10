#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// A played run, kept so it can be replayed exactly and read by tools (`jm
// session`): a folder with
//   session.json   what was played: game, seed, scene, frames, markers, end
//   frames.bin     each frame's dt (float32, in order) as the game advanced it,
//                  hitches clamped: live play's timing
//   inputs.jsonl   every input event the game saw, with its frame
//   timeline.jsonl the state every 30 frames (scene, session values, entity
//                  count, a hash of the entities to check replays by)
//   save.json      the save the run started from (replays start from it too)
//   thumbs/        a small JPEG every 60 frames (<frame>.jpg)
//   markers/       per marker (F8 while playing): <n>.png and <n>.json (state)
// Same seed, same save, same dts and the same input events at the same
// frames: the same run. (Gamepads are read, not evented: a run that used one
// says so, and its replay may differ.)
namespace session {

inline constexpr int kThumbWidth = 240;
inline constexpr int kFormat = 1;

// A cheap, stable hash of the state's entities (what a replay must match).
uint64_t entitiesHash(const nlohmann::json& state);

// Where a new play of the project goes: .jm/plays/<local date_time>, unused
// (_2, _3... after). .jm/ ignores itself, so plays never get committed.
std::filesystem::path newPlayDir(const std::filesystem::path& projectRoot);

// The game's state (Engine::stateJson(false)), made only when it's asked for.
using LazyState = std::function<const nlohmann::json&()>;

class Recorder {
 public:
  // meta: what's known at the start (game, seed, entryScene, window, ...).
  Recorder(std::filesystem::path dir, nlohmann::json meta, const std::string& startingSave);
  ~Recorder();

  // Frame `frame` starts, its scripts seeing the window focused or not.
  void frameStarts(uint64_t frame, bool focused);
  // An input the game got, at the frame running; between frames, at the last
  // one run (the game sees it from the next, as one at that frame's end).
  void input(nlohmann::json event);
  void gamepadUsed() { _meta["gamepad"] = true; }
  // After frame `frame` ran with `dt` (as the game advanced); `state` is asked
  // for on sampled frames. Returns where its thumbnail goes, on frames that get one.
  std::optional<std::filesystem::path> frameDone(uint64_t frame, float dt, const LazyState& state);
  // A marker at `frame`, numbered from 1; `image` is where its picture goes.
  struct Marker {
    int n;
    std::filesystem::path image;
  };
  Marker marker(uint64_t frame, double time, const nlohmann::json& state, const std::string& note = {});
  // The play is over (quit). `last`, the state after its last frame, ends the
  // timeline when that frame wasn't a sample: what happened since the last
  // one (a death in the final half second) isn't lost.
  void end(const nlohmann::json* last = nullptr);

 private:
  std::filesystem::path _dir;
  nlohmann::json _meta;
  std::ofstream _frames, _inputs, _timeline;
  uint64_t _framesRun = 0;
  std::optional<uint64_t> _running;  // the frame between frameStarts and frameDone
  double _seconds = 0.0;
  bool _ended = false;
  bool _focused = true;  // as recorded: a replay starts focused
  std::optional<uint64_t> _lastSample;
  void sample(uint64_t frame, const nlohmann::json& state);
  void writeMeta();
};

// A recorded session, for replaying it.
class Playback {
 public:
  // Throws std::runtime_error with what's wrong if `dir` isn't a session.
  explicit Playback(const std::filesystem::path& dir);

  const nlohmann::json& meta() const { return _meta; }
  uint64_t frames() const { return _dts.size(); }
  bool covers(uint64_t frame) const { return frame < _dts.size(); }
  float dt(uint64_t frame) const { return _dts[frame]; }
  const std::vector<nlohmann::json>& eventsAt(uint64_t frame) const;
  // The recorded hash for a sample frame, if it has one.
  std::optional<uint64_t> hashAt(uint64_t frame) const;
  // Whether the window had focus at `frame` (as the player's did).
  bool focusedAt(uint64_t frame) const;

 private:
  nlohmann::json _meta;
  std::vector<float> _dts;
  std::map<uint64_t, std::vector<nlohmann::json>> _events;
  std::map<uint64_t, uint64_t> _hashes;
  std::map<uint64_t, bool> _focus;  // changes, by frame
};

}  // namespace session
