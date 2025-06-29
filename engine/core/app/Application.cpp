#include "Application.hpp"

Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), _rootDir(rootDir), assetManager(_rootDir) {}

Application::~Application() {}

void Application::initialize() {
  moduleRegistry.initializeModules(*this);
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