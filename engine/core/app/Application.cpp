#include "Application.hpp"

Application::Application() {}

Application::~Application() {}

void Application::initialize(const std::filesystem::path& gameManifestPath) {
  _gameManifest = gameManifestPath;
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