#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "AgentTools.hpp"
#include "DevOptions.hpp"
#include "Seeds.hpp"
#include "EntitySpawner.hpp"
#include "EntityStores.hpp"
#include "GameClock.hpp"
#include "GameManifest.hpp"
#include "GameState.hpp"
#include "ModuleRegistry.hpp"
#include "Profiler.hpp"
#include "SceneManager.hpp"

// How an Engine runs. A standalone game uses the defaults (and Application's
// DevOptions::fromEnvironment); an editor embeds one.
struct EngineOptions {
  DevOptions dev;
  // No window of its own: the host sizes the view (resizeView), forwards input
  // events and draws each finished frame (Renderer2D::frameTexture).
  bool embedded = false;
  bool loadEntryScene = true;
  // A dedicated server (journeyman_server): built without the window,
  // renderer, UI and audio modules, so scripts' calls to those do nothing; it
  // starts in .jm.json's net.server.entryScene, and paces itself at
  // net.tickRate frames a second (nothing waits for a display).
  bool server = false;
};

// The runtime: world, assets, scripting, events, scenes, modules and the frame
// loop. initialize() loads the manifest and entry scene; run() loops until Quit,
// or a host calls frame() itself. The game advances in fixed steps (its
// frames: plays, the driver and captures count them); drawing goes between.
class Engine {
 public:
  Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath, EngineOptions options = {});
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Registers every component and binds every script host function (the
  // engine's and its modules'): the first step of initialize(), and on its own
  // all `--schema` needs (no window, GL or project). Idempotent.
  void declare();
  void initialize();
  void run();
  // A drawn frame `dt` seconds after the last (clamped to kMaxDeltaTime: a long
  // hitch slows the game rather than piling up steps): runs the steps that time
  // brings due, 0 or more, then draws between the last two. A replay's steps
  // take the recording's dts.
  void frame(float dt);
  // Exactly one step, then a draw of it as it ended (the driver's `step`, the editor's Step).
  void stepOnce();
  // Idempotent; the destructor calls it.
  void shutdown();
  // False once something asked to quit.
  bool running() const { return _running; }
  // Ends run() after the current frame.
  void quit() { _running = false; }
  // Inside a step (its systems and modules running): frameCount() is its own.
  bool stepping() const { return _inStep; }
  // Steps run: the game's frame number.
  uint64_t frameCount() const { return _frames; }
  // Seconds per step: 1 / .jm.json's config.stepRate (60 a second).
  float stepSeconds() const { return _stepSeconds; }
  // While drawing: where the frame lies between the last two steps, 0 the
  // earlier, 1 the last as it ended.
  float drawFraction() const { return _drawFraction; }
  // The game's state as data, for tools (state dumps, the driver; EngineState.cpp): frame,
  // scene, every entity's tags, parent and components (with their script
  // fields' values), the session and save stores, and modules' parts (UI).
  // withModules = false leaves out modules' parts (UI, the draw list): the
  // cheap core (what play sessions sample and hash).
  nlohmann::json stateJson(bool withModules = true);
  // The driver, live control, play recording and replay (AgentTools.hpp):
  // nothing at all in journeyman_player.
  AgentTools& agent() { return *_agent; }

  // Where frames' time goes (Profiler.hpp): on in a driven run (`profile`),
  // with JM_PROFILE (written there at exit), or when a host turns it on.
  // Null while off; modules add their counts (draw calls) to it.
  void setProfiling(bool on);
  Profiler* profiler() { return _profiler.get(); }

  // Off = an edit preview: rendering only, no scripts, physics or animation.
  void setSimulating(bool on) { _simulating = on; }
  bool simulating() const { return _simulating; }

  bool embedded() const { return _options.embedded; }
  // Windowed or fullscreen as the launch chose (JM_FULLSCREEN), over config
  // and saved settings; a replay has its recording's.
  std::optional<bool> launchFullscreen() const;
  bool server() const { return _options.server; }
  // Embedded only: the view's framebuffer size (its focus: setWindowFocused).
  void resizeView(int width, int height);
  struct ViewSize {
    int width = 0, height = 0;
  };
  ViewSize viewSize() const { return _viewSize; }
  // The size the game draws at, in framebuffer pixels (pointer positions are
  // in them too): the renderer's, from its start and each resize. A recorded
  // play keeps it, and its replay starts at it: a 2x screen's play replays
  // the same on a 1x one.
  void setFramebufferSize(int width, int height) { _framebuffer = {width, height}; }
  ViewSize framebufferSize() const { return _framebuffer; }

  // Whether the game's window has focus, as scripts see it: the window module
  // (or an embedding host, for its view) reports it; a replay answers what the player's had.
  void setWindowFocused(bool focused) { _windowFocused = focused; }
  bool windowFocused() const;
  // Input devices are ignored: the run is driven, or replays a recording
  // (the window module drops their events; the recording's come instead).
  bool devicesMuted() const { return _agent->devicesMuted(); }
  // Catching up with a session replay before the player takes over after it
  // (JM_PLAY_THEN=live): nothing is drawn, presented or heard until then.
  bool fastForwarding() const { return _agent->fastForwarding(); }
  // The agent tools hold the run (F8's moment): no steps and no sound until
  // they let go; frames are still drawn.
  bool held() const { return _agent->holding(); }

  // An image of the step now running, written by the renderer once it's drawn
  // (a .png, or a .jpg scaled down to maxWidth when that's set). Without
  // pixels (JM_RENDERER=none) requests are dropped.
  struct CaptureRequest {
    std::filesystem::path path;
    int maxWidth = 0;
  };
  void requestCapture(CaptureRequest request) { _captures.push_back(std::move(request)); }
  std::vector<CaptureRequest> takeCaptureRequests() { return std::exchange(_captures, {}); }

  // A short message for the player (the window's title shows it a moment).
  void notify(std::string message);
  // The current notice, "" once its moment has passed.
  std::string notice() const { return _frames < _noticeUntil ? _notice : std::string(); }

  World& getWorld() { return _world; }
  AssetManager& getAssetManager() { return _assetManager; }
  ScriptManager& getScriptManager() { return _scriptManager; }
  SceneManager& getSceneManager() { return _sceneManager; }
  EventBus& getEventBus() { return _eventBus; }
  GameClock& getClock() { return _clock; }
  Seeds& getSeeds() { return _seeds; }
  EntitySpawner& getSpawner() { return _spawner; }
  const GameManifest& getManifest() const { return _manifest; }
  const DevOptions& getDevOptions() const { return _options.dev; }
  ModuleRegistry& getModules() { return _modules; }
  // JM_ENTRY_SCENE, a replay's, the server's or the manifest's.
  std::string entrySceneName() const;
  // Reloads assets whose files changed and restarts the scene when one needs
  // it (or always, with `restartScene`). The paths it reloaded.
  std::vector<std::string> reloadAssets(bool restartScene);
  // Scripts' stores: the session's (GameState in scripts), the save's and each entity's (entity.data).
  GameState& getSession() { return _session; }
  GameState& getSave() { return *_save; }
  EntityStores& getEntityStores() { return _entityStores; }

 private:
  using Clock = std::chrono::steady_clock;
  static constexpr float kMaxDeltaTime = 0.1f;  // clamp hitches (no tunneling)

  EngineOptions _options;
  std::unique_ptr<AgentTools> _agent;  // made first: a replay's seed seeds the run
  std::unique_ptr<Profiler> _profiler;
  bool _windowFocused = true;
  std::vector<CaptureRequest> _captures;
  std::string _notice;
  uint64_t _noticeUntil = 0;  // frame
  std::filesystem::path _manifestPath;
  GameManifest _manifest;
  bool _initialized = false;
  bool _declared = false;
  bool _running = true;
  bool _simulating = true;
  ViewSize _viewSize;
  ViewSize _framebuffer;
  uint64_t _frames = 0;    // steps run; also the next one's number
  bool _inStep = false;    // inside step(): _frames is the current one
  float _stepSeconds = 1.0f / 60.0f;
  double _owed = 0.0;      // seconds the steps are behind the drawn frames (> 0: a step is due)
  float _drawFraction = 1.0f;
  void step();
  // Draws the frame after steps firstStep.. ran (none: the same game again).
  // started: when the frame began, for the profiler.
  void draw(float dt, uint64_t firstStep, Clock::time_point started);
  // Hands the profiler each system's or module's time; empty while not profiling.
  std::function<void(const char*, double)> timed(Profiler::Kind kind);
  void restartSceneWhenReady();
  std::string _sceneToRestart;  // hot reload: the scene to start again once it can
  bool _restartWaits = false;   // it couldn't: wait for the next file change

  World _world;
  AssetManager _assetManager;
  ScriptManager _scriptManager;
  EventBus _eventBus{8192};
  SceneManager _sceneManager;
  GameClock _clock;
  Seeds _seeds;  // JM_SEED, or a fresh one (logged) for a played run
  EntitySpawner _spawner;
  GameState _session;                   // shared script state for this run
  std::unique_ptr<GameState> _save;     // persisted; created once the game name is known
  EntityStores _entityStores;           // entity.data
  ModuleRegistry _modules;              // last: shut down and destroyed first

  void loadManifest();
  void registerScripting();
  void bindScriptApi();  // EngineScriptApi.cpp
  // An entity's ScriptComponent params, or null if it has no script.
  const nlohmann::json* paramsOf(EntityId id);
  void preloadAssets();
  void loadEntryScene();
};
