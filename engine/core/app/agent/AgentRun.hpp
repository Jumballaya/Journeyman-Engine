#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

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
  std::filesystem::path saveDir(std::filesystem::path usual) override;
  void start() override;

  bool runStarts() override;
  float beforeFrame() override;
  void runEnds() override;
  void frameDue() override;  // JM_WATCH: reloads what changed, every half second

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

  // F8's moment (Moment.cpp): the run held while the person picks what they
  // mean, shown and pointed at by moments/MomentsModule, then saved as a
  // marker with it. Frozen frames never step: a play has none.
  void hold();
  void resume();  // without saving
  // A pick: an entity, or a point or an area of the world (world units, y up).
  struct Pick {
    enum class Kind { Entity, Point, Area } kind = Kind::Entity;
    EntityId entity = kNoEntityId;
    glm::vec2 lo{0.0f}, hi{0.0f};  // a point's (both the same), an area's corners
    std::string note;              // what the person wrote about it
    // The same thing picked, whatever its note.
    bool operator==(const Pick& o) const { return kind == o.kind && entity == o.entity && lo == o.lo && hi == o.hi; }
  };
  // Picks `pick` alone; with `add`, adds it, or takes it away if it's picked.
  // An entity of kNoEntityId alone: none.
  void pick(const Pick& pick, bool add);
  std::vector<Pick> picks() const;  // those still alive
  // A pick as the driver and state dumps list it: its kind, and its entity, point or area.
  static nlohmann::json pickJson(const Pick& pick);
  // The note of pick n (1 is picks()' first), or the moment's for 0: saved
  // as the pick's "note" and the marker's. False when there's no pick n.
  bool setNote(size_t n, std::string note);
  std::string noteOf(size_t n) const;
  // `.`: one frame on before the next is drawn, still held.
  void stepHeld() { _stepHeld = true; }
  // A marker with the picks (an entity's origin and state; what's at a point
  // or in an area) and the notes, then the run goes on: its number, 0 when
  // the run isn't recorded. `note`, when given, is the moment's.
  int saveMoment(const std::string& note = {});
  // This frame is drawn without the moment's marks: the marker's picture of it is taken clean.
  bool drawsClean() const { return _cleanPending; }
  // Where an entity came from: a scene's entry, or a spawn (who spawned it,
  // when); null if neither. In state dumps' entities ("origin").
  nlohmann::json originOf(EntityId id) const;
  // What a saved pick adds (moments/MomentsModule says): its "rect" in the
  // frame's picture ([x, y, w, h] as fractions of it from its top-left), and
  // a point's or area's "entities" and "tiles" there. The agent's picture of
  // the moment outlines each by its rect.
  void setDescriber(std::function<nlohmann::json(const Pick&)> describe) { _describe = std::move(describe); }

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
  std::vector<Pick> _picks;
  std::string _note;  // the moment's
  std::unordered_map<EntityId, nlohmann::json> _origins;
  size_t _originsPruned = 0;  // how many were kept at the last prune
  std::function<nlohmann::json(const Pick&)> _describe;
  std::chrono::steady_clock::time_point _lastWatch{};  // JM_WATCH: when asset files were last checked

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
