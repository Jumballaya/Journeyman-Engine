#pragma once

#include <nlohmann/json_fwd.hpp>

class Engine;

class EngineModule {
 public:
  EngineModule() = default;
  virtual ~EngineModule() = default;

  EngineModule(const EngineModule&) = delete;
  EngineModule& operator=(const EngineModule&) = delete;

  EngineModule(EngineModule&&) = delete;
  EngineModule& operator=(EngineModule&&) = delete;

  // Registers the module's components (World::registerComponent) and nothing
  // else: it runs before initialize(), and alone for `journeyman_engine
  // --schema`, with no window, GL context or project.
  virtual void registerComponents(Engine&) {}
  // Binds the module's script host functions (ScriptManager::bind) and nothing
  // else: after registerComponents, before initialize; `--schema` lists them.
  virtual void bindScriptApi(Engine&) {}
  virtual void initialize(Engine& engine) = 0;
  virtual void shutdown(Engine& engine) = 0;
  // Adds the module's part of a state dump (Engine::stateJson), e.g. state["ui"].
  virtual void describeState(Engine&, nlohmann::json& /*state*/) {}
  // Each frame, after the systems (all on the main thread): OpenGL calls, window input...
  virtual void tickMainThread(Engine&, float) {}

  virtual const char* name() const { return "UNNAMED_MODULE"; }
};