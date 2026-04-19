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
#include "../scripting/ScriptHandle.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../scripting/ScriptSystem.hpp"
#include "ApplicationEvents.hpp"
#include "ApplicationHostFunctions.hpp"

Engine::Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _rootDir(rootDir),
      _manifestPath(manifestPath),
      _assetManager(_rootDir),
      _sceneLoader(_ecsWorld, _assetManager) {}

Engine::~Engine() = default;

void Engine::initialize() {
  setHostContext(*this);
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
  clearHostContext();
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
  _sceneLoader.loadScene(_manifest.entryScene);
  JM_LOG_INFO("[Scene Loaded]: {}", _sceneLoader.getCurrentSceneName());
}

void Engine::registerScriptModule() {
  _ecsWorld.registerComponent<ScriptComponent, PODScriptComponent>(
      // Deserialize JSON
      [&](World& world, EntityId id, const nlohmann::json& json) {
        // @TODO(asset-path-roundtrip): script name is not retained on the
        // component; the serializer below has no real path to write back.
        std::string manifestPath = json["script"].get<std::string>() + ".script.json";
        AssetHandle manifestHandle = _assetManager.loadAsset(manifestPath);
        const RawAsset& manifestAsset = _assetManager.getRawAsset(manifestHandle);
        nlohmann::json manifestJson = nlohmann::json::parse(std::string(
            manifestAsset.data.begin(),
            manifestAsset.data.end()));

        std::string wasmPath = manifestJson["binary"].get<std::string>();
        AssetHandle wasmHandle = _assetManager.loadAsset(wasmPath);
        const RawAsset& wasmAsset = _assetManager.getRawAsset(wasmHandle);

        std::vector<std::string> imports = manifestJson["imports"].get<std::vector<std::string>>();

        ScriptHandle scriptHandle = _scriptManager.loadScript(wasmAsset.data, imports);
        ScriptInstanceHandle instanceHandle = _scriptManager.createInstance(scriptHandle, id);
        world.addComponent<ScriptComponent>(id, instanceHandle);
      },
      // Serialize JSON
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<ScriptComponent>(id);
        if (!comp) {
          return false;
        }

        auto name = comp->name;

        auto instance = _scriptManager.getInstance(comp->instance);
        if (!instance) {
          return false;
        }

        auto scriptHandle = instance->getScriptHandle();
        auto script = _scriptManager.getScript(scriptHandle);
        if (!script) {
          return false;
        }

        // @TODO(asset-path-roundtrip): placeholder until component retains
        // its source asset path. See AssetManager.hpp "Known limitation".
        out["script"] = "path/to/script/without/ext";

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        // NO POD DATA FOR SCRIPTS FOR NOW
        return false;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        // NO POD DATA FOR SCRIPTS FOR NOW
        return false;
      });
  _ecsWorld.registerSystem<ScriptSystem>(_scriptManager);

  _scriptManager.initialize(*this);
  _scriptManager.registerHostFunction("__jmLog", {"env", "__jmLog", "v(ii)", &jmLog});
  _scriptManager.registerHostFunction("abort", {"env", "abort", "v(iiii)", &jmAbort});
  _scriptManager.registerHostFunction("__jmEcsGetComponent", {"env", "__jmEcsGetComponent", "i(iiii)", &jmEcsGetComponent});
  _scriptManager.registerHostFunction("__jmEcsUpdateComponent", {"env", "__jmEcsUpdateComponent", "i(iii)", &jmEcsUpdateComponent});
}
