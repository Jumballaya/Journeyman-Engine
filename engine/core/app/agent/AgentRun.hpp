#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../AgentTools.hpp"
#include "../../ecs/entity/EntityId.hpp"
#include "../DevOptions.hpp"
#include "LiveControl.hpp"
#include "PlaySession.hpp"

// The agent build's tools (AgentTools.hpp), for one run: what its DevOptions
// ask for, and nothing while they ask for nothing.
class AgentRun final : public AgentTools {
 public:
  AgentRun(Engine& engine, DevOptions dev);  // opens JM_PLAY_SESSION's recording
  ~AgentRun() override;

  std::optional<uint64_t> seed() const override;
  std::optional<std::string> entryScene() const override;
  std::optional<bool> fullscreen() const override;
  std::filesystem::path saveDir(std::filesystem::path usual) override;
  void start() override;

  bool runStarts() override;
  float beforeFrame() override;
  void runEnds() override;

  float stepDue(float dt) override;
  void stepBegun() override;
  void devicesDue() override;
  void stepDone(float dt) override;
  void frameDrawn(uint64_t firstStep) override;
  void assetsReloaded() override;
  void shutdown() override;
  void describeState(nlohmann::json& state) override;

  bool devicesMuted() const override;
  bool fastForwarding() const override;
  std::optional<bool> windowFocused() const override;
  bool holding() const override { return _held; }

  void recordInput(nlohmann::json event) override;
  void gamepadUsed() override;
  bool moment() override;  // F8: holds the run (Moment.cpp)
  int dropMarker(const std::string& note = {}) override;
  void noteError(nlohmann::json error) override;
  void drive(std::istream& in, std::ostream& out) override;  // EngineDriver.cpp
  nlohmann::json command(std::string line) override;

  // F8's moment (Moment.cpp): the run held while the person picks the
  // entities they mean, shown and pointed at by moments/MomentsModule, then
  // saved as a marker with them. Frozen frames never step: a play has none.
  void hold();
  void resume();  // without saving
  // Picks `id` alone; with `add`, toggles it among the picks. kNoEntityId alone: none.
  void pick(EntityId id, bool add);
  std::vector<EntityId> picks() const;  // those still alive
  // `.`: one frame on before the next is drawn, still held.
  void stepHeld() { _stepHeld = true; }
  // A marker with the picks (their origin and state), then the run goes on:
  // its number, 0 when the run isn't recorded.
  int saveMoment(const std::string& note = {});
  // This frame is drawn without the moment's marks: the marker's picture of it is taken clean.
  bool drawsClean() const { return _cleanPending; }
  // Where an entity came from: a scene's entry, or a spawn (who spawned it,
  // when); null if neither. In state dumps' entities ("origin").
  nlohmann::json originOf(EntityId id) const;
  // Where an entity is in the frame's picture (moments/MomentsModule says):
  // [x, y, w, h] as fractions of it from its top-left, null if it isn't drawn.
  // A saved pick's "rect": the agent's picture of the moment outlines it.
  void setWhereDrawn(std::function<nlohmann::json(EntityId)> where) { _whereDrawn = std::move(where); }

 private:
  Engine& _engine;
  const DevOptions _dev;
  std::unique_ptr<session::Playback> _playback;  // JM_PLAY_SESSION
  std::unique_ptr<session::Recorder> _recorder;  // JM_RECORD_DIR
  std::unique_ptr<LiveControl> _live;            // JM_CONTROL
  std::string _notRecordingNotice;               // F8's toast once a recording failed or ended
  std::optional<uint64_t> _divergedAt;           // a replay that didn't match its recording
  std::optional<uint64_t> _matchedAt;            // the last frame it did match at
  std::filesystem::path _replaySaveDir;          // a replay's copy of the player's save, deleted with the run
  std::vector<nlohmann::json> _driverErrors;
  bool _held = false;          // F8's moment
  bool _stepHeld = false;      // `.`
  bool _cleanPending = false;  // the held frame's picture, due once it's drawn (Moment.cpp)
  std::vector<EntityId> _picks;
  std::unordered_map<EntityId, nlohmann::json> _origins;
  size_t _originsPruned = 0;  // how many were kept at the last prune
  std::function<nlohmann::json(EntityId)> _whereDrawn;

  bool replayingAt(uint64_t frame) const;  // the recording covers frame (up to JM_PLAY_UNTIL)
  void startSession();                     // JM_SESSION, or a replay's starting values
  void startReplay();                      // the recording's framebuffer size, before the first frame
  void startRecording();
  void replay(const std::vector<nlohmann::json>& events);
  void giveInput(const nlohmann::json& event);  // the driver's set and scene, or a replay of one
  void dumpState(const std::string& name);      // JM_DUMP_DIR/<name>
  // The driver's `until <get> <op> <value> [max n]`: steps until a value compares true.
  nlohmann::json until(std::string_view args);
  // message with the errors logged since the last one.
  nlohmann::json withErrors(nlohmann::json message);
  // Moment.cpp: origins as entities are built and spawned; the driver's
  // freeze, resume, picks and save; a marker, with the picks or without.
  void trackOrigins();
  void keepOrigin(EntityId id, nlohmann::json origin);
  nlohmann::json momentCommand(const std::string& verb, const std::string& args);
  int mark(const std::string& note, bool withPicks);
  std::filesystem::path heldImage() const { return _dev.recordDir / "held.png"; }
};
