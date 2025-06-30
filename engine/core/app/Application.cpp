#include "Application.hpp"

#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptHandle.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../scripting/ScriptSystem.hpp"

Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), _rootDir(rootDir), _assetManager(_rootDir), _sceneLoader(_ecsWorld, _assetManager) {}

Application::~Application() {}

void Application::initialize() {
  AssetHandle manifestHandle = _assetManager.loadAsset(_manifestPath);
  const RawAsset& rawManifest = _assetManager.getRawAsset(manifestHandle);
  std::string jsonString(rawManifest.data.begin(), rawManifest.data.end());
  nlohmann::json jsonManifest = nlohmann::json::parse(jsonString);
  _manifest = parseGameManifest(jsonManifest);

  // Set up scripting ECS (Component and System)
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
        ScriptHandle scriptHandle = _scriptManager.loadScript(wasmAsset.data);
        ScriptInstanceHandle instanceHandle = _scriptManager.createInstance(scriptHandle);
        world.addComponent<ScriptComponent>(id, instanceHandle);
      });
  _ecsWorld.registerSystem<ScriptSystem>(_scriptManager);

  _scriptManager.initialize(*this);
  GetModuleRegistry().initializeModules(*this);

  std::cout << "[Game Loading]: " << _manifest.name << " v" << _manifest.version << "\n";
  std::cout << "[Scene Loading]: " << _manifest.entryScene << "\n";
  _sceneLoader.loadScene(_manifest.entryScene);
  std::cout << "[Scene Loaded]: " << _sceneLoader.getCurrentSceneName() << "\n";
}

GameManifest Application::parseGameManifest(const nlohmann::json& json) {
  GameManifest manifest;

  if (json.contains("name")) manifest.name = json["name"].get<std::string>();
  if (json.contains("version")) manifest.version = json["version"].get<std::string>();
  if (json.contains("entryScene")) manifest.entryScene = json["entryScene"].get<std::string>();
  if (json.contains("assets")) manifest.assets = json["assets"].get<std::vector<std::string>>();
  if (json.contains("scripts")) manifest.scenes = json["scenes"].get<std::vector<std::string>>();
  if (json.contains("config")) manifest.config = json["config"];

  return manifest;
}

void Application::run() {
  running = true;

  while (running) {
    float dt = 0.016f;

    _jobSystem.beginFrame();

    TaskGraph graph;
    _ecsWorld.buildExecutionGraph(graph, dt);
    GetModuleRegistry().buildAsyncTicks(graph, dt);
    _jobSystem.execute(graph);

    _jobSystem.endFrame();

    GetModuleRegistry().tickMainThreadModules(dt);
  }

  shutdown();
}

void Application::shutdown() {
  GetModuleRegistry().shutdownModules(*this);
}

World& Application::getWorld() { return _ecsWorld; }
JobSystem& Application::getJobSystem() { return _jobSystem; }
AssetManager& Application::getAssetManager() { return _assetManager; }
const GameManifest& Application::getManifest() const { return _manifest; }