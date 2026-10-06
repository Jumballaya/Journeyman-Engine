#pragma once

class Engine;

class EngineModule {
 public:
  EngineModule() = default;
  virtual ~EngineModule() = default;

  EngineModule(const EngineModule&) = delete;
  EngineModule& operator=(const EngineModule&) = delete;

  EngineModule(EngineModule&&) = delete;
  EngineModule& operator=(EngineModule&&) = delete;

  virtual void initialize(Engine& engine) = 0;
  virtual void shutdown(Engine& engine) = 0;
  // Main thread, each frame: OpenGL calls, window input...
  virtual void tickMainThread(Engine&, float) {}
  // A worker thread, each simulated frame: thread-safe work like physics.
  virtual void tickAsync(float) {}

  virtual const char* name() const { return "UNNAMED_MODULE"; }
};