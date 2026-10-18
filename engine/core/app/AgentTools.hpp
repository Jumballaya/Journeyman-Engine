#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

class Engine;
struct DevOptions;

// What the agent build adds to a run: the stepped driver (JM_DRIVE), live
// control (JM_CONTROL), play recording and replay (JM_RECORD_DIR,
// JM_PLAY_SESSION, F8's moments and markers), state dumps (JM_DUMP_DIR), profile output
// (JM_PROFILE), hot reload (JM_WATCH), JM_SESSION and JM_EXIT_AFTER_FRAMES.
// The engine calls these at fixed points of a run. The modules' own tools
// (input replays, captures, the driver's commands for them) are agent-only
// modules beside them: <module>/agent/, linked the same way.
//
// A run has one, made by whichever library the executable links:
// engine_agent_tools (journeyman_engine, the editor, the server, tests) makes
// the tools (agent/AgentRun.hpp); engine_agent_tools_none (journeyman_player,
// what exported games ship) makes this class itself, whose methods do
// nothing: the agent tooling isn't in a player at all. Neither changes how a
// game plays unless a JM_* switch (or a host's DevOptions) asks.
class AgentTools {
 public:
  // Throws if the run can't start as asked (a replay that can't be read).
  // `engine` is still being made: only used once the run starts.
  static std::unique_ptr<AgentTools> make(Engine& engine, const DevOptions& dev);
  virtual ~AgentTools() = default;

  // How the run starts. The seed and entry scene it must have (a replay's);
  // where its save is (a replay's copy of the player's, never theirs).
  virtual std::optional<uint64_t> seed() const { return std::nullopt; }
  virtual std::optional<std::string> entryScene() const { return std::nullopt; }
  virtual std::filesystem::path saveDir(std::filesystem::path usual) { return usual; }
  // Modules are up, the entry scene isn't loaded yet.
  virtual void start() {}

  // Engine::run. runStarts: true when the tools ran the game themselves (the
  // driver) and it's over. beforeFrame: how fast game time goes (0: paused).
  virtual bool runStarts() { return false; }
  virtual float beforeFrame() { return 1.0f; }
  virtual void runEnds() {}
  // A frame is due (Engine::frame, stepOnce): before anything of it runs.
  virtual void frameDue() {}

  // A step, in order: due (what came between frames is given; returns its dt,
  // a replay's own), begun (the bus delivered what came between steps),
  // devices due (where the window's events come in), done (may end the run).
  virtual float stepDue(float dt) { return dt; }
  virtual void stepBegun() {}
  virtual void devicesDue() {}
  virtual void stepDone(float /*dt*/) {}
  // A frame was drawn after steps firstStep.. ran.
  virtual void frameDrawn(uint64_t /*firstStep*/) {}
  // Files changed under a running game (hot reload).
  virtual void assetsReloaded() {}
  // Before the scene unloads at shutdown.
  virtual void shutdown() {}
  // The tools' part of Engine::stateJson (a replay's).
  virtual void describeState(nlohmann::json& /*state*/) {}

  // The run as the game sees it: input devices ignored (driven, replayed),
  // catching up with a replay (nothing drawn or heard), the window's focus as
  // a replay had it.
  virtual bool devicesMuted() const { return false; }
  virtual bool fastForwarding() const { return false; }
  virtual std::optional<bool> windowFocused() const { return std::nullopt; }
  // The tools hold the run (F8's moment): no steps, no sound, the game's
  // devices muted; the frame is still drawn.
  virtual bool holding() const { return false; }

  // For modules and hosts. An input the game got (recorded with the play); a
  // gamepad was read (pads aren't recorded); a marker in the recorded play
  // (the editor's F8): its number, 0 when the run isn't recorded; an error the run logged
  // (the driver's next reply says it).
  virtual void recordInput(nlohmann::json /*event*/) {}
  virtual void gamepadUsed() {}
  // F8 down: the person has something to show their agent. True when the
  // tools took the key (the game doesn't get it).
  virtual bool moment() { return false; }
  virtual int dropMarker(const std::string& /*note*/ = {}) { return 0; }
  virtual void noteError(nlohmann::json /*error*/) {}
  // The stepped driver (agent/EngineDriver.cpp): commands on `in`, a JSON
  // line each on `out`, until "quit", the end of input or a Quit. command: one
  // line of it, as the driver answers it (null for a blank line or comment).
  virtual void drive(std::istream& /*in*/, std::ostream& /*out*/) {}
  virtual nlohmann::json command(std::string /*line*/) { return nullptr; }
};
