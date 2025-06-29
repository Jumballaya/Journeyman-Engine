#include "Application.hpp"

Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), _rootDir(rootDir), _assetManager(_rootDir), _sceneLoader(_ecsWorld, _assetManager) {}

Application::~Application() {}

void Application::initialize() {
  AssetHandle manifestHandle = _assetManager.loadAsset(_manifestPath);
  const RawAsset& rawManifest = _assetManager.getRawAsset(manifestHandle);
  std::string jsonString(rawManifest.data.begin(), rawManifest.data.end());
  nlohmann::json jsonManifest = nlohmann::json::parse(jsonString);
  _manifest = parseGameManifest(jsonManifest);
  std::cout << "[Game Loading]: " << _manifest.name << " v" << _manifest.version << "\n";
  std::cout << "[Scene Loading]: " << _manifest.entryScene << "\n";
  _sceneLoader.loadScene(_manifest.entryScene);
  std::cout << "[Scene Loaded]: " << _sceneLoader.getCurrentSceneName() << "\n";

  _moduleRegistry.initializeModules(*this);
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
    _moduleRegistry.buildAsyncTicks(graph, dt);
    _jobSystem.execute(graph);

    _jobSystem.endFrame();

    _moduleRegistry.tickMainThreadModules(dt);
  }

  shutdown();
}

void Application::shutdown() {
  _moduleRegistry.shutdownModules();
}

World& Application::getWorld() { return _ecsWorld; }

JobSystem& Application::getJobSystem() { return _jobSystem; }

const GameManifest& Application::getManifest() const { return _manifest; }