#pragma once

#include <chrono>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "DevOptions.hpp"
#include "Seeds.hpp"
#include "EntitySpawner.hpp"
#include "EntityStores.hpp"
#include "GameClock.hpp"
#include "GameManifest.hpp"
#include "GameState.hpp"
#include "ModuleRegistry.hpp"
#include "PlaySession.hpp"
#include "SceneManager.hpp"

// How an Engine runs. A standalone game uses the defaults; an editor embeds one.
struct EngineOptions {
  DevOptions dev = DevOptions::fromEnvironment();
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
// or a host calls frame() itself.
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
  // One frame of `dt` seconds (clamped to kMaxDeltaTime); a replay's frames
  // take the recording's dt instead.
  void frame(float dt);
  // Idempotent; the destructor calls it.
  void shutdown();
  // False once something asked to quit.
  bool running() const { return _running; }
  // Ends run() after the current frame.
  void quit() { _running = false; }
  uint64_t frameCount() const { return _frames; }
  // The game's state as data, for tools (JM_DUMP_DIR; EngineState.cpp): frame,
  // scene, every entity's tags, parent and components (with their script
  // fields' values), the session and save stores, and modules' parts (UI).
  // withModules = false leaves out modules' parts (UI, the draw list): the
  // cheap core (what play sessions sample and hash).
  nlohmann::json stateJson(bool withModules = true);
  // The stepped driver (JM_DRIVE; EngineDriver.cpp): reads commands from `in`
  // and answers each with one JSON line on `out`, advancing only when told.
  // run() uses it when driving. Returns at "quit", end of input, or a Quit.
  void drive(std::istream& in, std::ostream& out);
  // Errors the run logs, reported in the driver's next reply (Application).
  void noteError(nlohmann::json error) { _driverErrors.push_back(std::move(error)); }

  // Off = an edit preview: rendering only, no scripts, physics or animation.
  void setSimulating(bool on) { _simulating = on; }
  bool simulating() const { return _simulating; }

  bool embedded() const { return _options.embedded; }
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
  bool devicesMuted() const;
  // A session replay is playing its recording (JM_PLAY_SESSION, up to
  // JM_PLAY_UNTIL); fast-forwarding when the player takes over after it
  // (JM_PLAY_THEN=live): nothing is drawn, presented or heard until then.
  bool replaying() const;
  bool fastForwarding() const;
  // Play sessions, for the modules that record their own input (the inputs
  // module's keys, by name; a replay gives them back as EVT_NamedKey).
  void recordInput(nlohmann::json event);
  // A gamepad was read this frame (sessions note it: pads aren't recorded).
  void noteGamepadUsed() {
    if (_recorder) _recorder->gamepadUsed();
  }

  // An image of the frame now ending (the last one drawn), written by the
  // renderer as the next frame starts (a .png, or a .jpg scaled down to
  // maxWidth when that's set). Without pixels (JM_RENDERER=none) requests are
  // dropped.
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

  // Marks this moment in the recorded session (F8 while playing): its number,
  // or 0 when the run isn't recorded.
  int dropMarker(const std::string& note = {});

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
  // Scripts' stores: the session's (GameState in scripts) and each entity's (entity.data).
  GameState& getSession() { return _session; }
  EntityStores& getEntityStores() { return _entityStores; }

 private:
  // The driver's `until <get> <op> <value> [max n]`: steps frames of dt until a value compares true.
  nlohmann::json until(std::string_view args, float dt);
  using Clock = std::chrono::steady_clock;
  static constexpr float kMaxDeltaTime = 0.1f;  // clamp hitches (no tunneling)

  EngineOptions _options;
  // JM_PLAY_SESSION: made first, its seed seeds the run.
  std::unique_ptr<session::Playback> _playback;
  std::unique_ptr<session::Recorder> _recorder;  // JM_RECORD_DIR
  std::optional<uint64_t> _divergedAt;            // a replay that didn't match its recording
  std::optional<uint64_t> _matchedAt;             // the last frame it did match at
  bool _windowFocused = true;
  std::vector<CaptureRequest> _captures;
  std::string _notice;
  uint64_t _noticeUntil = 0;  // frame
  std::filesystem::path _manifestPath;
  GameManifest _manifest;
  bool _initialized = false;
  bool _declared = false;
  std::vector<nlohmann::json> _driverErrors;
  bool _running = true;
  bool _simulating = true;
  ViewSize _viewSize;
  ViewSize _framebuffer;
  std::filesystem::path _replaySaveDir;  // a replay's copy of the player's save, deleted at shutdown
  uint64_t _frames = 0;    // frames run; also the next one's number
  bool _inFrame = false;   // inside frame(): _frames is the current one
  Clock::time_point _lastWatch{};  // JM_WATCH: when asset files were last checked
  void reloadChangedAssets();

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
  // Writes stateJson() to JM_DUMP_DIR/<name> when dumping is on.
  void dumpState(const std::string& name);
  // An entity's ScriptComponent params, or null if it has no script.
  const nlohmann::json* paramsOf(EntityId id);
  void preloadAssets();
  void loadSessionFile();  // JM_SESSION
  void loadEntryScene();
  std::string entrySceneName() const;  // JM_ENTRY_SCENE, a replay's, the server's or the manifest's
  // PlaySession: starts a replay and recording (JM_RECORD_DIR), and per frame:
  // feeds a replay's inputs, records, checks a replay against its recording.
  void startReplay();  // the recording's framebuffer size, before the first frame
  void startRecording();
  void replay(const std::vector<nlohmann::json>& events);
  void giveInput(const nlohmann::json& event);
  void sessionFrameDone(float dt);
  void recordFrame(float dt, const session::LazyState& state);
  void verifyFrame(const session::LazyState& state);  // against the recording's hash, where it has one
};
