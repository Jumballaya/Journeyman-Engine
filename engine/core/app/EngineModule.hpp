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
  // Each frame, after the systems (all on the main thread): OpenGL calls, window input...
  virtual void tickMainThread(Engine&, float) {}

  virtual const char* name() const { return "UNNAMED_MODULE"; }
};