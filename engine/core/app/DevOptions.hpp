#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Development / automation switches, read once from JM_* environment
// variables (documented in docs/testing.md). Empty/zero means "off". An
// exported game (journeyman_player) reads none: every switch stays off.
//
// An automated run (headless, or replaying input) is deterministic unless told
// otherwise: it steps a fixed 1/60 s and uses seed 1, so the same inputs give
// the same frames every time.
struct DevOptions {
  bool headless = false;                     // JM_HEADLESS: hidden window
  std::string renderer;                      // JM_RENDERER=none: no window or OpenGL at all (implies headless)
  float fixedDt = 0.0f;                      // JM_FIXED_DT: seconds per frame
  std::optional<uint64_t> seed;              // JM_SEED: every random number in the run follows from it
  uint64_t exitAfterFrames = 0;              // JM_EXIT_AFTER_FRAMES
  std::string entryScene;                    // JM_ENTRY_SCENE
  std::filesystem::path sessionFile;         // JM_SESSION: a JSON object of session values set before the first frame
  std::filesystem::path saveDir;             // JM_SAVE_DIR
  std::filesystem::path captureDir;          // JM_CAPTURE_DIR
  std::vector<uint64_t> captureFrames;       // JM_CAPTURE_FRAMES=60,120
  std::filesystem::path inputReplay;         // JM_INPUT_REPLAY
  bool drive = false;                        // JM_DRIVE: stepped by commands on stdin (AgentTools::drive)
  std::filesystem::path driveRecord;         // JM_DRIVE_RECORD: the driven inputs, as a replay file
  std::filesystem::path dumpDir;             // JM_DUMP_DIR: the game's state as JSON, at exit
  std::vector<uint64_t> dumpFrames;          // JM_DUMP_FRAMES=60,120: and at these frames
  std::filesystem::path inspectOut;          // JM_INSPECT: the entry scene as the engine builds it, as JSON there; nothing runs (agent/Inspect.cpp)
  std::string errorsOut;                     // JM_ERRORS: "-" (stderr) or a file; errors as JSON lines
  bool strict = false;                       // JM_STRICT: the first error ends the run, exit code 1
  std::filesystem::path profileOut;          // JM_PROFILE: where frames' time goes, as JSON at exit (Profiler.hpp)
  bool debugPhysics = false;                 // JM_DEBUG_PHYSICS: draw colliders and terrain over the frame
  std::optional<bool> debugDraw;             // JM_DEBUG_DRAW=0|1: scripts' Debug drawing (default: on, but off in an exported game)
  std::optional<std::pair<int, int>> windowPos;  // JM_WINDOW_POS=x,y: where the window opens (jm run --peers)
  std::optional<bool> fullscreen;            // JM_FULLSCREEN=0|1: open windowed or fullscreen, whatever config and saved settings say
  bool realtime = false;                     // JM_REALTIME: a fixed-dt run still keeps to the clock (multiplayer tests)
  bool watch = false;                        // JM_WATCH: reload images, atlases, shaders, sounds and scripts whose files change
  std::filesystem::path control;             // JM_CONTROL: a live run steered through files there (agent/LiveControl.hpp)
  std::filesystem::path recordDir;           // JM_RECORD_DIR: record this (played) run as a session there (agent/PlaySession.hpp)
  std::filesystem::path playSession;         // JM_PLAY_SESSION: replay a recorded session, exactly
  std::optional<uint64_t> playUntil;         // JM_PLAY_UNTIL=n: the recording ends at frame n (a marker's, say)

  // Networking (docs/networking.md): host or join as the game starts, and
  // trouble on purpose, to see a game cope.
  std::string netHost;                       // JM_NET_HOST=1 (or a port)
  std::string netJoin;                       // JM_NET_JOIN=host:port
  std::string netName;                       // JM_NET_NAME: this player's name
  uint16_t netPort = 0;                      // JM_NET_PORT: the port to host on
  float netLatency = 0.0f;                   // JM_NET_LATENCY, ms
  float netJitter = 0.0f;                    // JM_NET_JITTER, ms
  float netLoss = 0.0f;                      // JM_NET_LOSS, 0..1
  std::filesystem::path netTrace;            // JM_NET_TRACE: one JSON line per message

  // What a replay does once its recording ends: the run stops, the player
  // takes over (JM_PLAY_THEN=live), or the driver goes on stepping (JM_DRIVE wins).
  enum class AfterReplay { Stop, Live, Drive };
  AfterReplay afterReplay = AfterReplay::Stop;

  // Defined per executable, by the library it links: engine_agent_tools
  // reads the environment (journeyman_engine, journeyman_server, tests),
  // engine_agent_tools_none returns the defaults (journeyman_player).
  static DevOptions fromEnvironment();

  // A replay that hands over to the player isn't automated: it ends live.
  bool automated() const {
    return headless || drive || !inputReplay.empty() || (!playSession.empty() && afterReplay != AfterReplay::Live);
  }
};
