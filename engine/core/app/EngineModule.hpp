#pragma once

class Application;

class EngineModule {
 public:
  EngineModule() = default;
  virtual ~EngineModule() = default;

  EngineModule(const EngineModule&) = delete;
  EngineModule& operator=(const EngineModule&) = delete;

  EngineModule(EngineModule&&) = delete;
  EngineModule& operator=(EngineModule&&) = delete;

  virtual void initialize(Application& app) = 0;
  virtual void shutdown() = 0;
  virtual void tickMainThread(float dt) {};  // For main-thread tasks like OpenGL calls, GLFW inputs, etc.
  virtual void tickAsync(float dt) {};       // For thread-safe jobs that need to be ran per frame like kick off asset loading, physics update, etc.
};