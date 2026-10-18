#include "Engine.hpp"

#include <algorithm>
#include <exception>
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

Engine::Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath, EngineOptions options)
    : _options(std::move(options)),
      _agent(AgentTools::make(*this, _options.dev)),
      _manifestPath(manifestPath),
      _assetManager(rootDir),
      _sceneManager(_world, _assetManager, _eventBus),
      _seeds(_agent->seed() ? *_agent->seed()
                            : _options.dev.seed.value_or((uint64_t{std::random_device{}()} << 32) | std::random_device{}())),
      _spawner(_world, _assetManager, _sceneManager) {
  for (const auto& add : ModuleCatalog()) add(_modules);
}

Engine::~Engine() { shutdown(); }

std::optional<bool> Engine::launchFullscreen() const {
  return _agent->fullscreen();
}

void Engine::initialize() {
  // Scenes, prefabs and the manifest are read by the engine itself; these
  // no-op archive types just mark them as known.
  for (const char* type : {"manifest", "scene", "prefab"}) {
    _assetManager.addAssetTypeConverter(type, [](const RawAsset&, const AssetHandle&) {});
  }

  _initialized = true;  // from here on, shutdown has modules to stop
  JM_LOG_INFO("[Engine] seed {} (JM_SEED={} repeats this run's randomness)", _seeds.seed(), _seeds.seed());
  loadManifest();
  const auto saveDir = _options.dev.saveDir.empty() ? platform::userDataDir(_manifest.name) : _options.dev.saveDir;
  _save = std::make_unique<GameState>(_agent->saveDir(saveDir) / "save.json");

  // Scene entries' "if" / "unless" read the session's game state.
  _sceneManager.setCondition([this](const std::string& key) {
    const auto value = _session.getJson(key);
    if (!value) return false;
    if (value->is_boolean()) return value->get<bool>();
    if (value->is_number()) return value->get<double>() != 0.0;
    if (value->is_string()) return !value->get<std::string>().empty();
    return !value->is_null() && !value->empty();
  });
  // A scene that started ran the files as they are: a failed hot restart goes back to those.
  _sceneManager.addLoadListener([this] { _assetManager.keepReloads(); });

  declare();
  registerScripting();
  _scriptManager.setStubMissingImports(_options.server);
  _modules.initializeModules(*this);
  preloadAssets();
  _eventBus.subscribe<events::WindowResized>(EVT_WindowResize, [this](const events::WindowResized& e) {
    setFramebufferSize(e.width, e.height);
  });
  _agent->start();  // before the entry scene: its entries' if/unless read the session
  if (_options.loadEntryScene) loadEntryScene();

  // A pause never leaks into the next scene (e.g. "Main Menu" from a pause menu).
  _eventBus.subscribe<events::SceneLoaded>(EVT_SceneLoaded, [this](const events::SceneLoaded&) {
    _clock.setScale(1.0f);
  });
  _eventBus.subscribe<events::Quit>(EVT_AppQuit, [this](const events::Quit&) { _running = false; });
}

void Engine::run() {
  const auto start = Clock::now();
  auto previous = start;
  // A server has no display to wait for: it runs a frame a tick and sleeps
  // between. An automated run with a fixed dt runs flat out, unless it's
  // asked to keep to the clock (JM_REALTIME: talking to other processes).
  const bool fixed = _options.dev.fixedDt > 0.0f;
  const double tickRate = fixed ? 1.0 / _options.dev.fixedDt : std::clamp(_manifest.net.value("tickRate", 60.0), 1.0, 1000.0);
  const bool paced = (_options.server && !fixed) || (_options.dev.realtime && fixed);
  const auto tick = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / tickRate));
  auto nextTick = start;
  const bool driven = _agent->runStarts();
  while (_running && !driven) {
    const float scale = _agent->beforeFrame();
    const auto now = Clock::now();
    const float measured = std::chrono::duration<float>(now - previous).count();
    previous = now;
    if (paced) {
      frame(scale * static_cast<float>(1.0 / tickRate));
      nextTick += tick;
      // Fell behind (a long frame): start counting again from now, no catch-up burst.
      if (nextTick < Clock::now()) nextTick = Clock::now();
      std::this_thread::sleep_until(nextTick);
    } else {
      frame(scale * (_options.dev.fixedDt > 0.0f ? _options.dev.fixedDt : measured));
    }
  }

  _agent->runEnds();
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  if (_frames > 0) {
    JM_LOG_INFO("[Engine] {} frames in {:.1f}s ({:.2f} ms/frame avg)", _frames, seconds, seconds * 1000.0 / _frames);
  }
  shutdown();
}

void Engine::reloadChangedAssets() {
  if (!_options.dev.watch || Clock::now() - _lastWatch < std::chrono::milliseconds(500)) return;
  _lastWatch = Clock::now();
  reloadAssets(false);
}

std::vector<std::string> Engine::reloadAssets(bool restartScene) {
  const AssetManager::Reloaded reloaded = _assetManager.reloadChanged();
  std::string names;
  for (const std::string& path : reloaded.paths) {
    JM_LOG_INFO("[Engine] reloaded {}", path);
    names += (names.empty() ? "" : ", ") + std::filesystem::path(path).filename().string();
  }
  if (!names.empty()) notify("reloaded " + names);
  if ((restartScene || reloaded.restartScene) && !_sceneManager.getCurrentScenePath().empty()) {
    _sceneToRestart = _sceneManager.getCurrentScenePath();
  }
  if (_sceneToRestart.empty()) _assetManager.keepReloads();  // no scene to restart: nothing to undo
  if (!reloaded.paths.empty() || restartScene) _restartWaits = false;  // something new to try with
  if (!reloaded.paths.empty() || !_sceneToRestart.empty()) _agent->assetsReloaded();
  restartSceneWhenReady();
  return reloaded.paths;
}

void Engine::restartSceneWhenReady() {
  if (_sceneToRestart.empty() || _restartWaits || _sceneManager.isTransitioning()) return;
  const std::string& current = _sceneManager.getCurrentScenePath();
  if (!current.empty() && current != _sceneToRestart) {  // the game went to another scene meanwhile
    _sceneToRestart.clear();
    return;
  }
  // A scene file that isn't valid JSON yet (half saved) waits for its next change: a failed
  // start would leave nothing running.
  _restartWaits = true;
  try {
    const auto& bytes = _assetManager.getRawAsset(_assetManager.loadAsset(_sceneToRestart)).data;
    if (nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false).is_discarded()) return;
    // GameState carries over, as across any scene change. A failure goes back to the files it last started with.
    _sceneManager.loadScene(_sceneToRestart, [this](const std::exception& e) {
      JM_REPORT_ERROR((ErrorSource{_sceneToRestart}),
                      "[Engine] {} didn't restart (it will on the next change); it runs its last files that worked: {}",
                      _sceneToRestart, e.what());
      _assetManager.undoReloads();
    });
    _sceneToRestart.clear();
  } catch (const std::exception& e) {
    JM_REPORT_ERROR((ErrorSource{_sceneToRestart}), "[Engine] {} didn't restart (it will on the next change): {}",
                    _sceneToRestart, e.what());
  }
}

void Engine::frame(float dt) {
  const auto started = _profiler ? Clock::now() : Clock::time_point{};
  reloadChangedAssets();
  restartSceneWhenReady();
  const uint64_t first = _frames;
  dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDeltaTime) : 0.0f;
  if (!_simulating) {
    _clock.advance(dt);  // an edit preview's shaders still animate
    _owed = 0.0;
  } else if (fastForwarding()) {
    step();  // catching up with a replay: a step a frame, as fast as they run
    _owed = 0.0;
    dt = _clock.unscaledDt();
  } else {
    // A frame that's a step long (give or take rounding: JM_FIXED_DT=0.0166667) is exactly one.
    _owed += std::abs(dt - _stepSeconds) < _stepSeconds * 1e-3f ? _stepSeconds : dt;
    for (; _owed > 0.0 && _running; _owed -= _stepSeconds) step();
  }
  draw(dt, first, started);
}

void Engine::stepOnce() {
  const auto started = _profiler ? Clock::now() : Clock::time_point{};
  reloadChangedAssets();
  restartSceneWhenReady();
  const uint64_t first = _frames;
  if (_simulating) step();
  _owed = 0.0;
  draw(_stepSeconds, first, started);
}

void Engine::step() {
  // A replay's dt is the recording's (older plays kept a hitch unclamped).
  const float dt = std::min(_agent->stepDue(_stepSeconds), kMaxDeltaTime);
  // Scene changes asked for last step take effect, so what's drawn after a step is its systems' work.
  _sceneManager.tick(_clock.unscaledDt());
  _eventBus.dispatch();  // what came between steps (a marker then is of the last step)
  _inStep = true;
  _agent->stepBegun();
  _clock.advance(dt);

  // Everything runs on this thread, in the same order every step: systems
  // (scripts included), then what scripts queued, then modules (input, network) and events.
  _world.runSystems(_clock.dt(), SystemStage::Input, SystemStage::PostPhysics, timed(Profiler::Kind::System));
  _spawner.flush();
  _entityStores.prune(_world);
  _agent->devicesDue();  // where the window's events come in
  _modules.stepModules(*this, _clock.unscaledDt(), timed(Profiler::Kind::Module));
  _eventBus.dispatch();
  _save->flush();
  _agent->stepDone(dt);
  ++_frames;
  _inStep = false;
}

void Engine::draw(float dt, uint64_t firstStep, Clock::time_point started) {
  const double fraction = 1.0 + _owed / _stepSeconds;
  _drawFraction = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
  _modules.drawModules(*this, dt, timed(Profiler::Kind::Module));
  _agent->frameDrawn(firstStep);
  if (!_simulating) {
    _eventBus.dispatch();  // an edit preview runs no steps: its events are delivered here
    _save->flush();
  }
  // The profiler's frame is a drawn one, of however many steps it ran.
  if (!_profiler) return;
  _scriptManager.takeTimes([this](const std::string& script, double s) { _profiler->add(Profiler::Kind::Script, script, s); });
  _profiler->add(Profiler::Kind::Count, "entities", static_cast<double>(_world.entityCount()));
  _profiler->add(Profiler::Kind::Count, "scripts", static_cast<double>(_scriptManager.instanceCount()));
  _profiler->endFrame(std::chrono::duration<double>(Clock::now() - started).count());
}

std::function<void(const char*, double)> Engine::timed(Profiler::Kind kind) {
  if (!_profiler) return {};
  return [this, kind](const char* name, double seconds) { _profiler->add(kind, name, seconds); };
}

void Engine::setProfiling(bool on) {
  if (on == (_profiler != nullptr)) return;
  _profiler = on ? std::make_unique<Profiler>() : nullptr;
  _scriptManager.setTimed(on);
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
  _agent->shutdown();
  // Entities first: their destroy hooks reach into modules.
  _sceneManager.unload();
  _eventBus.dispatch();
  if (_save) _save->flush();
  _modules.shutdownModules(*this);
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
  if (_manifest.config.is_object()) {
    const nlohmann::json rate = _manifest.config.value("stepRate", nlohmann::json(60));
    // At least 20: physics moves at most 0.05 s a call (simulationStep), and scripts must get the same dt.
    _stepSeconds = 1.0f / static_cast<float>(std::clamp(rate.is_number() ? rate.get<double>() : 60.0, 20.0, 1000.0));
  }
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

bool Engine::windowFocused() const { return _agent->windowFocused().value_or(_windowFocused); }

void Engine::notify(std::string message) {
  _notice = std::move(message);
  _noticeUntil = _frames + 150;
}

std::string Engine::entrySceneName() const {
  if (auto scene = _agent->entryScene()) return *scene;
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
      .onDestroy = [this](ScriptComponent& c) {
        // Not on a scene unload: what it spawned would land in the next scene.
        ScriptInstance* instance = _scriptManager.getInstance(c.instance);
        if (instance && !_sceneManager.unloading()) instance->onDestroy();
        _scriptManager.destroyInstance(c.instance);
      },
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
  _assetManager.addAssetConverter({".ts"}, loadScript, AssetManager::Reload::InPlace);
  _assetManager.addAssetTypeConverter("script", loadScript);

  _world.registerSystem<ScriptSystem>(_scriptManager, _clock);
}
