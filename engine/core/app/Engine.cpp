#include "Engine.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptSystem.hpp"
#include "ApplicationEvents.hpp"
#include "Platform.hpp"

Engine::Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath),
      _assetManager(rootDir),
      _sceneManager(_world, _assetManager, _eventBus),
      _spawner(_world, _assetManager, _sceneManager) {}

Engine::~Engine() = default;

void Engine::initialize() {
  // Scenes, prefabs and the manifest are read by the engine itself; these
  // no-op archive types just mark them as known.
  for (const char* type : {"manifest", "scene", "prefab"}) {
    _assetManager.addAssetTypeConverter(type, [](const RawAsset&, const AssetHandle&) {});
  }

  loadManifest();
  const auto saveDir = _dev.saveDir.empty() ? platform::userDataDir(_manifest.name) : _dev.saveDir;
  _save = std::make_unique<GameState>(saveDir / "save.json");

  registerScripting();
  GetModuleRegistry().initializeModules(*this);
  preloadAssets();
  loadEntryScene();

  // A pause never leaks into the next scene (e.g. "Main Menu" from a pause menu).
  _eventBus.subscribe<events::SceneLoaded>(EVT_SceneLoaded, [this](const events::SceneLoaded&) {
    _clock.setScale(1.0f);
  });
  _eventBus.subscribe<events::Quit>(EVT_AppQuit, [this](const events::Quit&) { _running = false; });
}

void Engine::run() {
  _running = true;
  const auto start = Clock::now();
  auto previous = start;
  uint64_t frames = 0;

  while (_running) {
    const auto now = Clock::now();
    const float measured = std::chrono::duration<float>(now - previous).count();
    previous = now;
    _clock.advance(std::min(_dev.fixedDt > 0.0f ? _dev.fixedDt : measured, kMaxDeltaTime));

    TaskGraph graph;
    _world.buildExecutionGraph(graph, _clock.dt());
    GetModuleRegistry().buildAsyncTicks(graph, _clock.dt());
    _jobSystem.execute(graph);

    // Main thread from here on: apply what scripts queued, then let modules
    // (window, input, rendering) and scenes advance.
    _spawner.flush();
    _entityStores.prune(_world);
    GetModuleRegistry().tickMainThreadModules(*this, _clock.unscaledDt());
    _sceneManager.tick(_clock.unscaledDt());
    _eventBus.dispatch();
    _save->flush();

    ++frames;
    if (_dev.exitAfterFrames > 0 && frames >= _dev.exitAfterFrames) _running = false;
  }

  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  if (frames > 0) {
    JM_LOG_INFO("[Engine] {} frames in {:.1f}s ({:.2f} ms/frame avg)", frames, seconds, seconds * 1000.0 / frames);
  }
  shutdown();
}

void Engine::shutdown() {
  JM_LOG_INFO("[Engine] Shutting down");
  _save->flush();
  GetModuleRegistry().shutdownModules(*this);
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
  const std::string& scene = _dev.entryScene.empty() ? _manifest.entryScene : _dev.entryScene;
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
  });
  _world.registerSystem<ScriptSystem>(_scriptManager, _clock);
  bindScriptApi();
}
