#include "Engine.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptSystem.hpp"
#include "ApplicationEvents.hpp"
#include "WindowEvents.hpp"
#include "Platform.hpp"

Engine::Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath, EngineOptions options)
    : _options(std::move(options)),
      _manifestPath(manifestPath),
      _assetManager(rootDir),
      _sceneManager(_world, _assetManager, _eventBus),
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
  loadManifest();
  const auto saveDir = _options.dev.saveDir.empty() ? platform::userDataDir(_manifest.name) : _options.dev.saveDir;
  _save = std::make_unique<GameState>(saveDir / "save.json");

  registerScripting();
  _modules.initializeModules(*this);
  preloadAssets();
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
  while (_running) {
    const auto now = Clock::now();
    const float measured = std::chrono::duration<float>(now - previous).count();
    previous = now;
    frame(_options.dev.fixedDt > 0.0f ? _options.dev.fixedDt : measured);
    if (_options.dev.exitAfterFrames > 0 && _frames >= _options.dev.exitAfterFrames) _running = false;
  }

  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  if (_frames > 0) {
    JM_LOG_INFO("[Engine] {} frames in {:.1f}s ({:.2f} ms/frame avg)", _frames, seconds, seconds * 1000.0 / _frames);
  }
  shutdown();
}

void Engine::frame(float dt) {
  _clock.advance(std::min(dt, kMaxDeltaTime));

  TaskGraph graph;
  _world.buildExecutionGraph(graph, _clock.dt(), _simulating ? SystemStage::Input : SystemStage::Render);
  if (_simulating) _modules.buildAsyncTicks(graph, _clock.dt());
  _jobSystem.execute(graph);

  // Main thread from here on: apply what scripts queued, then let modules
  // (window, input, rendering) and scenes advance.
  _spawner.flush();
  _entityStores.prune(_world);
  _modules.tickMainThreadModules(*this, _clock.unscaledDt());
  if (_simulating) _sceneManager.tick(_clock.unscaledDt());
  _eventBus.dispatch();
  _save->flush();
  ++_frames;
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
  JM_LOG_INFO("[Engine] {} v{}", _manifest.name, _manifest.version);
}

void Engine::preloadAssets() {
  for (const auto& path : _manifest.assets) {
    try {
      _assetManager.loadAsset(path);
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[Engine] failed to load asset '{}': {}", path, e.what());
    }
  }
}

void Engine::loadEntryScene() {
  const std::string& scene = _options.dev.entryScene.empty() ? _manifest.entryScene : _options.dev.entryScene;
  if (scene.empty()) {
    JM_LOG_WARN("[Engine] no entry scene");
    return;
  }
  _sceneManager.loadScene(scene);
}

void Engine::registerScripting() {
  // `jm build` writes compiled wasm at each script's .ts path (folder mode);
  // archives tag the same bytes with type "script".
  auto loadScript = [this](const RawAsset& asset, const AssetHandle& handle) {
    _scriptManager.loadScript(handle, asset.data, asset.filePath.generic_string());
  };
  _assetManager.addAssetConverter({".ts"}, loadScript);
  _assetManager.addAssetTypeConverter("script", loadScript);

  _world.registerComponent<ScriptComponent>({
      .fromJson = [this](ScriptComponent& c, const nlohmann::json& json, EntityId id) {
        const std::string path = json.value("script", std::string());
        c.params = json.value("params", nlohmann::json::object());
        c.runWhenPaused = json.value("runWhenPaused", false);
        try {
          c.script = _assetManager.loadAsset(path);
        } catch (const std::exception& e) {
          JM_LOG_ERROR("[Script] '{}' failed to load: {}", path, e.what());
          c.started = true;  // nothing to start
        }
      },
      .onDestroy = [this](ScriptComponent& c) { _scriptManager.destroyInstance(c.instance); },
      .schema = {"Script", "Core", "Runs an AssemblyScript behavior",
                 {FieldSchema::asset("script", {".ts"}, "The script file"),
                  FieldSchema::json("params", "Values the script reads with me.params"),
                  FieldSchema::boolean("runWhenPaused", false, "Keep running while the game is paused")}},
  });
  _world.registerSystem<ScriptSystem>(_scriptManager, _clock);
  bindScriptApi();
}
