#include "Engine.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptSystem.hpp"
#include "ApplicationEvents.hpp"
#include "WindowEvents.hpp"
#include "Platform.hpp"

namespace {

// JM_PLAY_SESSION's recording; throws (the run can't start) if it can't be read.
std::unique_ptr<session::Playback> openPlayback(const DevOptions& dev) {
  if (dev.playSession.empty()) return nullptr;
  return std::make_unique<session::Playback>(dev.playSession);
}

}  // namespace

Engine::Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath, EngineOptions options)
    : _options(std::move(options)),
      _playback(openPlayback(_options.dev)),
      _manifestPath(manifestPath),
      _assetManager(rootDir),
      _sceneManager(_world, _assetManager, _eventBus),
      _seeds(_playback ? _playback->meta().value("seed", uint64_t{1})
                       : _options.dev.seed.value_or((uint64_t{std::random_device{}()} << 32) | std::random_device{}())),
      _spawner(_world, _assetManager, _sceneManager) {
  for (const auto& add : ModuleCatalog()) add(_modules);
}

Engine::~Engine() { shutdown(); }

void Engine::initialize() {
  // Scenes, prefabs and the manifest are read by the engine itself; these
  // no-op archive types just mark them as known.
  for (const char* type : {"manifest", "scene", "prefab"}) {
    _assetManager.addAssetTypeConverter(type, [](const RawAsset&, const AssetHandle&) {});
  }

  _initialized = true;  // from here on, shutdown has modules to stop
  JM_LOG_INFO("[Engine] seed {} (JM_SEED={} repeats this run's randomness)", _seeds.seed(), _seeds.seed());
  loadManifest();
  auto saveDir = _options.dev.saveDir.empty() ? platform::userDataDir(_manifest.name) : _options.dev.saveDir;
  if (_playback && _options.dev.saveDir.empty()) {
    // A replay starts from the save the player had, and never touches theirs.
    saveDir = std::filesystem::temp_directory_path() /
              ("jm-replay-" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(saveDir);
    _replaySaveDir = saveDir;
    std::error_code ec;
    std::filesystem::copy_file(_options.dev.playSession / "save.json", saveDir / "save.json", ec);
  }
  _save = std::make_unique<GameState>(saveDir / "save.json");

  // Scene entries' "if" / "unless" read the session's game state.
  _sceneManager.setCondition([this](const std::string& key) {
    const auto value = _session.getJson(key);
    if (!value) return false;
    if (value->is_boolean()) return value->get<bool>();
    if (value->is_number()) return value->get<double>() != 0.0;
    if (value->is_string()) return !value->get<std::string>().empty();
    return !value->is_null() && !value->empty();
  });

  declare();
  registerScripting();
  _scriptManager.setStubMissingImports(_options.server);
  _modules.initializeModules(*this);
  preloadAssets();
  loadSessionFile();  // before the entry scene: its entries' if/unless read the session
  startRecording();
  if (_options.loadEntryScene) loadEntryScene();

  // A pause never leaks into the next scene (e.g. "Main Menu" from a pause menu).
  _eventBus.subscribe<events::SceneLoaded>(EVT_SceneLoaded, [this](const events::SceneLoaded&) {
    _clock.setScale(1.0f);
  });
  _eventBus.subscribe<events::Quit>(EVT_AppQuit, [this](const events::Quit&) { _running = false; });
  _eventBus.subscribe<events::WindowResized>(EVT_WindowResize, [this](const events::WindowResized& e) {
    setFramebufferSize(e.width, e.height);
  });
}

void Engine::run() {
  const auto start = Clock::now();
  auto previous = start;
  // A server has no display to wait for: it steps a fixed tick and sleeps
  // between. An automated run with a fixed dt runs flat out, unless it's
  // asked to keep to the clock (JM_REALTIME: talking to other processes).
  const bool fixed = _options.dev.fixedDt > 0.0f;
  const double tickRate = fixed ? 1.0 / _options.dev.fixedDt : std::clamp(_manifest.net.value("tickRate", 60.0), 1.0, 1000.0);
  const bool paced = (_options.server && !fixed) || (_options.dev.realtime && fixed);
  const auto tick = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / tickRate));
  auto nextTick = start;
  // A replay plays its recording and stops, unless the player takes over then.
  const bool replayStops = _playback && !_options.dev.playThenLive && !_options.dev.drive;
  if (_options.dev.drive) drive(std::cin, std::cout);
  while (_running && !_options.dev.drive) {
    const auto now = Clock::now();
    const float measured = std::chrono::duration<float>(now - previous).count();
    previous = now;
    if (paced) {
      frame(static_cast<float>(1.0 / tickRate));
      nextTick += tick;
      // Fell behind (a long frame): start counting again from now, no catch-up burst.
      if (nextTick < Clock::now()) nextTick = Clock::now();
      std::this_thread::sleep_until(nextTick);
    } else {
      frame(stepDt(_options.dev.fixedDt > 0.0f ? _options.dev.fixedDt : measured));
    }
    if (_options.dev.exitAfterFrames > 0 && _frames >= _options.dev.exitAfterFrames) _running = false;
    if (replayStops && !replaying()) _running = false;
  }

  dumpState("state_exit.json");
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  if (_frames > 0) {
    JM_LOG_INFO("[Engine] {} frames in {:.1f}s ({:.2f} ms/frame avg)", _frames, seconds, seconds * 1000.0 / _frames);
  }
  shutdown();
}

void Engine::frame(float dt) {
  _inFrame = true;
  _clock.advance(std::min(dt, kMaxDeltaTime));

  // Everything runs on this thread, in the same order every frame: systems
  // (scripts included), then what scripts queued, then modules (window, input,
  // rendering) and scenes.
  _world.runSystems(_clock.dt(), _simulating ? SystemStage::Input : SystemStage::Render);
  _spawner.flush();
  _entityStores.prune(_world);
  replayInputs();  // where the window module's events would come in
  _modules.tickMainThreadModules(*this, _clock.unscaledDt());
  // The frame as drawn (and captured): before scene changes take effect.
  const auto& dumps = _options.dev.dumpFrames;
  if (!_options.dev.dumpDir.empty() && std::find(dumps.begin(), dumps.end(), _frames) != dumps.end()) {
    char name[32];
    std::snprintf(name, sizeof(name), "state_%05llu.json", static_cast<unsigned long long>(_frames));
    dumpState(name);
  }
  if (_simulating) _sceneManager.tick(_clock.unscaledDt());
  _eventBus.dispatch();
  _save->flush();
  sessionFrameDone(dt);
  ++_frames;
  _inFrame = false;
}

void Engine::resizeView(int width, int height) {
  if (width <= 0 || height <= 0 || (width == _viewSize.width && height == _viewSize.height)) return;
  _viewSize = {width, height};
  _eventBus.emit(EVT_WindowResize, events::WindowResized{width, height});
}

void Engine::shutdown() {
  if (!_initialized) return;
  _initialized = false;
  _running = false;
  JM_LOG_INFO("[Engine] Shutting down");
  if (_recorder) {
    const auto last = stateJson(false);
    _recorder->end("quit", &last);
  }
  // Entities first: their destroy hooks reach into modules.
  _sceneManager.unload();
  _eventBus.dispatch();
  if (_save) _save->flush();
  _modules.shutdownModules(*this);
  if (!_replaySaveDir.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(_replaySaveDir, ec);
  }
}

void Engine::loadManifest() {
  const RawAsset& raw = _assetManager.getRawAsset(_assetManager.loadAsset(_manifestPath));
  const nlohmann::json json = nlohmann::json::parse(raw.data.begin(), raw.data.end());
  _manifest.name = json.value("name", _manifest.name);
  _manifest.version = json.value("version", _manifest.version);
  _manifest.entryScene = json.value("entryScene", _manifest.entryScene);
  _manifest.assets = json.value("assets", _manifest.assets);
  _manifest.scenes = json.value("scenes", _manifest.scenes);
  _manifest.config = json.value("config", nlohmann::json::object());
  _manifest.net = json.value("net", nlohmann::json::object());
  if (!_manifest.net.is_object()) _manifest.net = nlohmann::json::object();
  JM_LOG_INFO("[Engine] {} v{}", _manifest.name, _manifest.version);
}

void Engine::preloadAssets() {
  for (const auto& path : _manifest.assets) {
    try {
      _assetManager.loadAsset(path);
    } catch (const std::exception& e) {
      JM_REPORT_ERROR((ErrorSource{path}), "[Engine] failed to load asset '{}': {}", path, e.what());
    }
  }
}

void Engine::startRecording() {
  if (_options.dev.recordDir.empty()) return;
  std::string startingSave;
  if (std::ifstream in(_save->file()); in) startingSave.assign(std::istreambuf_iterator<char>(in), {});
  const nlohmann::json config = _manifest.config.is_object() ? _manifest.config : nlohmann::json::object();
  nlohmann::json meta = {{"game", _manifest.name},
                         {"gameVersion", _manifest.version},
                         {"seed", _seeds.seed()},
                         {"entryScene", entrySceneName()},
                         {"sessionValues", _session.values()},
                         {"window", config.value("window", nlohmann::json::object())},
                         {"framebuffer", {_framebuffer.width, _framebuffer.height}},
                         {"gamepad", false}};
  if (_playback) meta["replayOf"] = _options.dev.playSession.filename().string();
  try {
    _recorder = std::make_unique<session::Recorder>(_options.dev.recordDir, std::move(meta), startingSave);
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[Session] not recording: {}", e.what());
    return;
  }
  JM_LOG_INFO("[Session] recording to {} (F8 drops a marker)", _options.dev.recordDir.string());

  // The pointer, wheel and window, at the frame the game sees them (dispatch).
  // Keys are recorded by the inputs module, by name: scancodes differ
  // between machines, a key's name doesn't.
  auto record = [this](nlohmann::json event) { _recorder->input(_frames, std::move(event)); };
  _eventBus.subscribe<events::MouseMove>(EVT_MouseMove, [record](const events::MouseMove& e) {
    record({{"type", "move"}, {"x", e.x}, {"y", e.y}});
  });
  _eventBus.subscribe<events::MouseButton>(EVT_MouseButton, [record](const events::MouseButton& e) {
    record({{"type", "button"}, {"button", e.button}, {"down", e.down}});
  });
  _eventBus.subscribe<events::MouseWheel>(EVT_MouseWheel, [record](const events::MouseWheel& e) {
    record({{"type", "wheel"}, {"dx", e.dx}, {"dy", e.dy}});
  });
  _eventBus.subscribe<events::WindowResized>(EVT_WindowResize, [record](const events::WindowResized& e) {
    record({{"type", "resize"}, {"w", e.width}, {"h", e.height}});
  });
}

void Engine::replayInputs() {
  if (!replaying()) return;
  // Floats went through JSON as doubles: they come back bit for bit. (Keys
  // are the inputs module's: recordedInputs.)
  if (_frames == 0) {
    // The recording's size first: pointer positions are in its pixels.
    const nlohmann::json size = _playback->meta().value("framebuffer", nlohmann::json());
    const int w = size.is_array() && size.size() == 2 && size[0].is_number_integer() ? size[0].get<int>() : 0;
    const int h = w > 0 && size[1].is_number_integer() ? size[1].get<int>() : 0;
    if (w > 0 && h > 0) _eventBus.emit(EVT_WindowResize, events::WindowResized{w, h});
  }
  for (const nlohmann::json& e : _playback->eventsAt(_frames)) {
    const std::string type = e.value("type", "");
    if (type == "move") _eventBus.emit(EVT_MouseMove, events::MouseMove{e.value("x", 0.0f), e.value("y", 0.0f)});
    else if (type == "button") _eventBus.emit(EVT_MouseButton, events::MouseButton{e.value("button", 0), e.value("down", false)});
    else if (type == "wheel") _eventBus.emit(EVT_MouseWheel, events::MouseWheel{e.value("dx", 0.0f), e.value("dy", 0.0f)});
    else if (type == "resize") _eventBus.emit(EVT_WindowResize, events::WindowResized{e.value("w", 0), e.value("h", 0)});
  }
}

void Engine::sessionFrameDone(float dt) {
  const bool sample = _frames % session::kSampleEvery == 0;
  if (_recorder) {
    if (_frames % session::kThumbEvery == 0) {
      char name[32];
      std::snprintf(name, sizeof(name), "%06llu.jpg", static_cast<unsigned long long>(_frames));
      requestCapture({_recorder->dir() / "thumbs" / name, session::kThumbWidth});
    }
    nlohmann::json state;
    if (sample) state = stateJson(false);
    _recorder->frameDone(_frames, dt, sample ? &state : nullptr);
  }
  if (replaying() && sample && !_divergedAt) {
    if (auto recorded = _playback->hashAt(_frames); recorded && *recorded != session::entitiesHash(stateJson(false))) {
      _divergedAt = _frames;
      JM_LOG_WARN("[Session] this replay differs from its recording from frame {} on (by frame {} at the latest)",
                  _frames > session::kSampleEvery ? _frames - session::kSampleEvery + 1 : 0, _frames);
    }
  }
}

void Engine::recordInput(uint64_t frame, nlohmann::json event) {
  if (_recorder) _recorder->input(frame, std::move(event));
}

const std::vector<nlohmann::json>& Engine::recordedInputs() const {
  static const std::vector<nlohmann::json> none;
  return replaying() ? _playback->eventsAt(_frames) : none;
}

float Engine::stepDt(float live) const {
  return replaying() ? _playback->dt(_frames) : live;
}

void Engine::setWindowFocused(bool focused) {
  if (focused == _windowFocused) return;
  _windowFocused = focused;
  // Scripts read it from the next frame they run: inside a frame (the window
  // module's tick, after the systems) that's the next one.
  if (_recorder) _recorder->input(_inFrame ? _frames + 1 : _frames, {{"type", "focus"}, {"focused", focused}});
}

bool Engine::windowFocused() const {
  return replaying() ? _playback->focusedAt(_frames) : _windowFocused;
}

bool Engine::devicesMuted() const {
  return _options.dev.drive || !_options.dev.inputReplay.empty() || replaying();
}

bool Engine::replaying() const {
  return _playback && _playback->covers(_frames) && (!_options.dev.playUntil || _frames < *_options.dev.playUntil);
}

bool Engine::fastForwarding() const { return _options.dev.playThenLive && replaying(); }

void Engine::notify(std::string message) {
  _notice = std::move(message);
  _noticeUntil = _frames + 150;
}

int Engine::dropMarker(const std::string& note) {
  if (!_recorder) return 0;
  const nlohmann::json state = stateJson();
  // The frame the marker is about: the one running (F8, seen as it ends), or
  // between frames (the driver's marker) the last one run, as the state says.
  const uint64_t frame = state.value("frame", uint64_t{0});
  const int n = _recorder->marker(frame, _clock.unscaledElapsed(), state, note);
  requestCapture({_recorder->dir() / "markers" / (std::to_string(n) + ".png")});
  notify("marker " + std::to_string(n) + " saved");
  JM_LOG_INFO("[Session] marker {} at frame {}", n, frame);
  return n;
}

std::string Engine::entrySceneName() const {
  if (_playback) return _playback->meta().value("entryScene", _manifest.entryScene);
  std::string scene = _options.dev.entryScene.empty() ? _manifest.entryScene : _options.dev.entryScene;
  if (_options.server && _options.dev.entryScene.empty()) {
    const nlohmann::json server = _manifest.net.value("server", nlohmann::json::object());
    if (server.is_object()) scene = server.value("entryScene", scene);
  }
  return scene;
}

void Engine::loadEntryScene() {
  const std::string scene = entrySceneName();
  if (scene.empty()) {
    JM_LOG_WARN("[Engine] no entry scene");
    return;
  }
  _sceneManager.loadScene(scene);
}

void Engine::loadSessionFile() {
  if (_playback) {  // the values the recorded run started with
    // A named copy: items() of the temporary value() returns would outlive it.
    const nlohmann::json values = _playback->meta().value("sessionValues", nlohmann::json::object());
    for (const auto& [key, value] : values.items()) {
      _session.setJson(key, value);
    }
    return;
  }
  const auto& path = _options.dev.sessionFile;
  if (path.empty()) return;
  std::ifstream in(path);
  const nlohmann::json values = nlohmann::json::parse(in, nullptr, false);
  if (!in.is_open() || !values.is_object()) {
    JM_REPORT_ERROR((ErrorSource{path.generic_string()}), "[Engine] JM_SESSION: '{}' isn't a readable JSON object",
                    path.string());
    return;
  }
  for (const auto& [key, value] : values.items()) _session.setJson(key, value);
  JM_LOG_INFO("[Engine] session: {} values from {}", values.size(), path.string());
}

void Engine::declare() {
  if (_declared) return;
  _declared = true;
  _world.registerComponent<ScriptComponent>({
      .fromJson = [this](ScriptComponent& c, const nlohmann::json& json, EntityId id) {
        const std::string path = json.value("script", std::string());
        c.params = json.value("params", nlohmann::json::object());
        c.runWhenPaused = json.value("runWhenPaused", false);
        // No script chosen yet, or an edit preview that never runs scripts.
        if (path.empty() || !_simulating) {
          c.started = true;
          return;
        }
        try {
          c.script = _assetManager.loadAsset(path);
        } catch (const std::exception& e) {
          JM_REPORT_ERROR((ErrorSource{path}), "[Script] '{}' failed to load: {}", path, e.what());
          c.started = true;  // nothing to start
        }
      },
      .onDestroy = [this](ScriptComponent& c) { _scriptManager.destroyInstance(c.instance); },
      .schema = {"Script", "Core", "Runs an AssemblyScript behavior",
                 {FieldSchema::asset("script", {".ts"}, "The script file"),
                  FieldSchema::json("params", "Values the script reads with me.params"),
                  FieldSchema::boolean("runWhenPaused", false, "Keep running while the game is paused")}},
  });
  _modules.registerComponents(*this);
  bindScriptApi();
  _modules.bindScriptApis(*this);
}

void Engine::registerScripting() {
  // `jm build` writes compiled wasm at each script's .ts path (folder mode);
  // archives tag the same bytes with type "script".
  auto loadScript = [this](const RawAsset& asset, const AssetHandle& handle) {
    _scriptManager.loadScript(handle, asset.data, asset.filePath.generic_string());
  };
  _assetManager.addAssetConverter({".ts"}, loadScript);
  _assetManager.addAssetTypeConverter("script", loadScript);

  _world.registerSystem<ScriptSystem>(_scriptManager, _clock);
}
