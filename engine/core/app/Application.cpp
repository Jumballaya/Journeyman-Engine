#include "Application.hpp"

Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), _rootDir(rootDir), assetManager(_rootDir) {}

Application::~Application() {}

void Application::initialize() {
  AssetHandle manifestHandle = assetManager.loadAsset(_manifestPath);
  const RawAsset& rawManifest = assetManager.getRawAsset(manifestHandle);
  std::string jsonString(rawManifest.data.begin(), rawManifest.data.end());
  nlohmann::json jsonManifest = nlohmann::json::parse(jsonString);
  _manifest = parseGameManifest(jsonManifest);

  std::cout << "Game: " << _manifest.name << " v" << _manifest.version << "\n";

  moduleRegistry.initializeModules(*this);
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

    jobSystem.beginFrame();

    TaskGraph graph;
    ecsWorld.buildExecutionGraph(graph, dt);
    moduleRegistry.buildAsyncTicks(graph, dt);
    jobSystem.execute(graph);

    jobSystem.endFrame();

    moduleRegistry.tickMainThreadModules(dt);
  }

  shutdown();
}

void Application::shutdown() {
  moduleRegistry.shutdownModules();
}

World& Application::getWorld() { return ecsWorld; }

JobSystem& Application::getJobSystem() { return jobSystem; }

const GameManifest& Application::getManifest() const { return _manifest; }