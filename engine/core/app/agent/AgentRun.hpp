#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "../AgentTools.hpp"
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

  void recordInput(nlohmann::json event) override;
  void gamepadUsed() override;
  int dropMarker(const std::string& note = {}) override;
  void noteError(nlohmann::json error) override;
  void drive(std::istream& in, std::ostream& out) override;  // EngineDriver.cpp
  nlohmann::json command(std::string line) override;

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
};
