#include "Engine.hpp"

#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "../assets/AssetHandle.hpp"
#include "../assets/RawAsset.hpp"
#include "../events/EventType.hpp"
#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../scripting/ScriptSystem.hpp"
#include "ApplicationEvents.hpp"
#include "ApplicationHostFunctions.hpp"
#include "SceneHostFunctions.hpp"

namespace {
// Context for ScriptComponent's onDestroy hook. Set in Engine::initialize
// before ScriptComponent is registered, cleared in Engine::shutdown. Same
// pattern as the input-host-context plumbing — keep ComponentInfo's onDestroy
// slot a raw function pointer (no std::function captures).
ScriptManager *s_scriptComponentOnDestroyContext = nullptr;

void scriptComponentOnDestroy(void *componentPtr) {
  auto *comp = static_cast<ScriptComponent *>(componentPtr);
  if (s_scriptComponentOnDestroyContext && comp->instance.isValid()) {
    s_scriptComponentOnDestroyContext->destroyInstance(comp->instance);
  }
}
}  // namespace

Engine::Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _rootDir(rootDir),
      _manifestPath(manifestPath),
      _assetManager(_rootDir),
      _sceneManager(_ecsWorld, _assetManager, _eventBus) {}

Engine::~Engine() = default;

void Engine::initialize() {
  setHostContext(*this);
  setSceneHostContext(*this, _sceneManager);
  s_scriptComponentOnDestroyContext = &_scriptManager;
  loadAndParseManifest();
  registerScriptModule();
  GetModuleRegistry().initializeModules(*this);
  initializeGameFiles();
  loadScenes();

  _eventBus.subscribe<events::Quit>(EVT_AppQuit, [this](const events::Quit& e) {
    (void)e;
    _running = false;
  });
}

void Engine::run() {
  _running = true;
  _previousFrameTime = Clock::now();

  while (_running) {
    auto currentTime = Clock::now();
    std::chrono::duration<float> deltaTime = currentTime - _previousFrameTime;
    _previousFrameTime = currentTime;

    float dt = deltaTime.count();
    if (dt > _maxDeltaTime) {
      dt = _maxDeltaTime;
    }

    _jobSystem.beginFrame();

    TaskGraph graph;
    _ecsWorld.buildExecutionGraph(graph, dt);
    GetModuleRegistry().buildAsyncTicks(graph, dt);
    _jobSystem.execute(graph);

    _jobSystem.endFrame();

    GetModuleRegistry().tickMainThreadModules(*this, dt);

    _sceneManager.tick(dt);

    _eventBus.dispatch();
  }
  shutdown();
}

void Engine::abort() {
  if (!_running) {
    shutdown();
    _running = false;
    std::exit(1);
    return;
  }
  _running = false;
}

void Engine::shutdown() {
  JM_LOG_INFO("[Engine] Shutting down");
  GetModuleRegistry().shutdownModules(*this);
  clearSceneHostContext();
  clearHostContext();
  s_scriptComponentOnDestroyContext = nullptr;
}

World& Engine::getWorld() { return _ecsWorld; }
JobSystem& Engine::getJobSystem() { return _jobSystem; }
AssetManager& Engine::getAssetManager() { return _assetManager; }
ScriptManager& Engine::getScriptManager() { return _scriptManager; }

void Engine::loadAndParseManifest() {
  AssetHandle manifestHandle = _assetManager.loadAsset(_manifestPath);
  const RawAsset& rawManifest = _assetManager.getRawAsset(manifestHandle);
  std::string jsonString(rawManifest.data.begin(), rawManifest.data.end());
  nlohmann::json json = nlohmann::json::parse(jsonString);

  if (json.contains("name")) _manifest.name = json["name"].get<std::string>();
  if (json.contains("version")) _manifest.version = json["version"].get<std::string>();
  if (json.contains("entryScene")) _manifest.entryScene = json["entryScene"].get<std::string>();
  if (json.contains("assets")) _manifest.assets = json["assets"].get<std::vector<std::string>>();
  if (json.contains("scenes")) _manifest.scenes = json["scenes"].get<std::vector<std::string>>();
  if (json.contains("config")) _manifest.config = json["config"];

  JM_LOG_INFO("[Engine Loading]: {} v{}", _manifest.name, _manifest.version);
}

void Engine::initializeGameFiles() {
  for (const auto& assetPath : _manifest.assets) {
    try {
      _assetManager.loadAsset(assetPath);
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[Asset Load Error]: Failed to load '{}' | {}", assetPath, e.what());
    }
  }
}

void Engine::loadScenes() {
  if (_manifest.entryScene.empty()) {
    JM_LOG_INFO("[Scene Loading]: no entry scene");
    return;
  }
  JM_LOG_INFO("[Scene Loading]: {}", _manifest.entryScene);
  _sceneManager.loadScene(_manifest.entryScene);
  JM_LOG_INFO("[Scene Loaded]: {}", _sceneManager.getCurrentScenePath());
}

void Engine::registerScriptModule() {
  // Converter for .script.json: parses the manifest, nested-loads the .wasm
  // (dedups via AssetManager), parses the module into a LoadedScript keyed by
  // the .script.json's AssetHandle. Preload does the real work; scene
  // deserialize is just a lookup.
  _assetManager.addAssetConverter({".script.json"},
      [this](const RawAsset& asset, const AssetHandle& manifestHandle) {
        nlohmann::json manifestJson = nlohmann::json::parse(std::string(
            asset.data.begin(), asset.data.end()));

        std::string wasmPath = manifestJson["binary"].get<std::string>();
        AssetHandle wasmHandle = _assetManager.loadAsset(wasmPath);
        const RawAsset& wasmAsset = _assetManager.getRawAsset(wasmHandle);

        std::vector<std::string> imports;
        if (manifestJson.contains("imports")) {
          imports = manifestJson["imports"].get<std::vector<std::string>>();
        }

        _scriptManager.loadScript(manifestHandle, wasmAsset.data, imports);
      });

  _ecsWorld.registerComponent<ScriptComponent, PODScriptComponent>(
      // Deserialize JSON: resolve scene reference → AssetHandle → instance.
      // loadAsset dedupes by path so preload and this lookup share a handle.
      [&](World& world, EntityId id, const nlohmann::json& json) {
        std::string scriptPath = json["script"].get<std::string>();
        try {
          AssetHandle scriptAsset = _assetManager.loadAsset(scriptPath);
          ScriptInstanceHandle inst = _scriptManager.createInstance(scriptAsset, id);
          if (!inst.isValid()) {
            JM_LOG_ERROR("[ScriptComponent] createInstance failed for '{}'; entity {}:{} skipped",
                         scriptPath, id.index, id.generation);
            return;
          }
          world.addComponent<ScriptComponent>(id, inst);
        } catch (const std::exception& e) {
          JM_LOG_ERROR("[ScriptComponent] load failed for '{}': {}", scriptPath, e.what());
        }
      },
      // Serialize JSON — round-trip deferred until components carry their
      // source AssetHandle. See AssetManager.hpp "Known limitation".
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<ScriptComponent>(id);
        if (!comp) return false;

        auto instance = _scriptManager.getInstance(comp->instance);
        if (!instance) return false;

        // @TODO(asset-path-roundtrip): placeholder until the component retains
        // its source AssetHandle and AssetManager exposes getPathByHandle.
        out["script"] = "path/to/script.script.json";
        return true;
      },
      // POD deserialize — scripts have no POD form today.
      [&](World& world, EntityId id, std::span<const std::byte> in) { return false; },
      // POD serialize — same.
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) { return false; },
      // onDestroy: release the wasm instance when the entity dies. Without
      // this, scene unloads leak ScriptInstance entries in ScriptManager.
      &scriptComponentOnDestroy);

  _ecsWorld.registerSystem<ScriptSystem>(_scriptManager);

  _scriptManager.initialize(*this);
  _scriptManager.registerHostFunction("__jmLog", {"env", "__jmLog", "v(ii)", &jmLog});
  _scriptManager.registerHostFunction("abort", {"env", "abort", "v(iiii)", &jmAbort});
  _scriptManager.registerHostFunction("__jmEcsGetComponent", {"env", "__jmEcsGetComponent", "i(iiii)", &jmEcsGetComponent});
  _scriptManager.registerHostFunction("__jmEcsUpdateComponent", {"env", "__jmEcsUpdateComponent", "i(iii)", &jmEcsUpdateComponent});
  _scriptManager.registerHostFunction("__jmSceneLoad", {"env", "__jmSceneLoad", "v(ii)", &jmSceneLoad});
  _scriptManager.registerHostFunction("__jmSceneTransition", {"env", "__jmSceneTransition", "v(iif)", &jmSceneTransition});
  _scriptManager.registerHostFunction("__jmSceneIsTransitioning", {"env", "__jmSceneIsTransitioning", "i()", &jmSceneIsTransitioning});
}
