#include "Application.hpp"

#include <cstdlib>
#include <stdexcept>

#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptHandle.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../scripting/ScriptSystem.hpp"

Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), _rootDir(rootDir), _assetManager(_rootDir), _sceneLoader(_ecsWorld, _assetManager) {}

Application::~Application() {}

void Application::initialize() {
  loadAndParseManifest();
  registerScriptModule();
  GetModuleRegistry().initializeModules(*this);
  initializeGameFiles();
  loadScenes();

  _eventBus.subscribe<events::Quit>([this](const events::Quit& e) {
    _running = false;
  });
}

void Application::run() {
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

    _eventBus.dispatch();

    GetModuleRegistry().tickMainThreadModules(*this, dt);
  }
  shutdown();
}

void Application::abort() {
  if (!_running) {
    shutdown();
    _running = false;
    std::exit(1);
    return;
  }
  _running = false;
}

void Application::shutdown() {
  JM_LOG_INFO("[Application] Shutting down");
  GetModuleRegistry().shutdownModules(*this);
}

World& Application::getWorld() { return _ecsWorld; }
JobSystem& Application::getJobSystem() { return _jobSystem; }
AssetManager& Application::getAssetManager() { return _assetManager; }
ScriptManager& Application::getScriptManager() { return _scriptManager; }

const GameManifest& Application::getManifest() const { return _manifest; }

void Application::initializeGameFiles() {
  for (const auto& assetPath : _manifest.assets) {
    try {
      _assetManager.loadAsset(assetPath);
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[Asset Load Error]: Failed to load '{}' | {}", assetPath, e.what());
    }
  }
}

void Application::loadAndParseManifest() {
  AssetHandle manifestHandle = _assetManager.loadAsset(_manifestPath);
  const RawAsset& rawManifest = _assetManager.getRawAsset(manifestHandle);
  std::string jsonString(rawManifest.data.begin(), rawManifest.data.end());
  nlohmann::json json = nlohmann::json::parse(jsonString);

  if (json.contains("name")) _manifest.name = json["name"].get<std::string>();
  if (json.contains("version")) _manifest.version = json["version"].get<std::string>();
  if (json.contains("entryScene")) _manifest.entryScene = json["entryScene"].get<std::string>();
  if (json.contains("assets")) _manifest.assets = json["assets"].get<std::vector<std::string>>();
  if (json.contains("scripts")) _manifest.scenes = json["scenes"].get<std::vector<std::string>>();
  if (json.contains("config")) _manifest.config = json["config"];

  JM_LOG_INFO("[Game Loading]: {} v{}", _manifest.name, _manifest.version);
}

void Application::registerScriptModule() {
  _ecsWorld.registerComponent<ScriptComponent>(
      [&](World& world, EntityId id, const nlohmann::json& json) {
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
        ScriptInstanceHandle instanceHandle = _scriptManager.createInstance(scriptHandle);
        world.addComponent<ScriptComponent>(id, instanceHandle);
      });
  _ecsWorld.registerSystem<ScriptSystem>(_scriptManager);

  _scriptManager.initialize(*this);
}

void Application::loadScenes() {
  JM_LOG_INFO("[Scene Loading]: {}", _manifest.entryScene);
  _sceneLoader.loadScene(_manifest.entryScene);
  JM_LOG_INFO("[Scene Loaded]: {}", _sceneLoader.getCurrentSceneName());
}