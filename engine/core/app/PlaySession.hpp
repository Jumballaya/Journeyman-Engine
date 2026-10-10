#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// A played run, kept so it can be replayed exactly and read by tools (`jm
// session`): a folder with
//   session.json   what was played: game, seed, scene, frames, markers, end
//   frames.bin     each frame's dt (float32, in order): live play's timing
//   inputs.jsonl   every input event the game saw, with its frame
//   timeline.jsonl the state every kSampleEvery frames (scene, session values,
//                  entity count, a hash of the entities to check replays by)
//   save.json      the save the run started from (replays start from it too)
//   thumbs/        a small JPEG every kThumbEvery frames
//   markers/       per marker (F8 while playing): <n>.png and <n>.json (state)
// Same seed, same save, same dts and the same input events at the same
// frames: the same run. (Gamepads are read, not evented: a run that used one
// says so, and its replay may differ.)
namespace session {

inline constexpr uint64_t kSampleEvery = 30;  // 0.5 s at 60 fps
inline constexpr uint64_t kThumbEvery = 60;
inline constexpr int kThumbWidth = 240;
inline constexpr int kFormat = 1;

// A cheap, stable hash of the state's entities (what a replay must match).
uint64_t entitiesHash(const nlohmann::json& state);

class Recorder {
 public:
  // meta: what's known at the start (game, seed, entryScene, window, ...).
  Recorder(std::filesystem::path dir, nlohmann::json meta, const std::string& startingSave);
  ~Recorder();

  const std::filesystem::path& dir() const { return _dir; }
  void input(uint64_t frame, nlohmann::json event);
  void gamepadUsed() { _meta["gamepad"] = true; }
  // After frame `frame` ran with `dt`; `state` is non-null on sample frames.
  void frameDone(uint64_t frame, float dt, const nlohmann::json* state);
  // A marker at `frame`: its number (1, 2, ...); writes markers/<n>.json.
  int marker(uint64_t frame, double time, const nlohmann::json& state, const std::string& note = {});
  // The play is over (quit). `last`, the state after its last frame, ends the
  // timeline when that frame wasn't a sample: what happened since the last
  // one (a death in the final half second) isn't lost.
  void end(const nlohmann::json* last = nullptr);

 private:
  std::filesystem::path _dir;
  nlohmann::json _meta;
  std::ofstream _frames, _inputs, _timeline;
  uint64_t _framesRun = 0;
  double _seconds = 0.0;
  bool _ended = false;
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
