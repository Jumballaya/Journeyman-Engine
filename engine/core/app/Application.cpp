#include "Application.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "../ecs/components/TransformComponent.hpp"
#include "../events/EventType.hpp"
#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "../scripting/ScriptHandle.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../scripting/ScriptSystem.hpp"
#include "ApplicationEvents.hpp"
#include "ApplicationHostFunctions.hpp"

Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), _rootDir(rootDir), _assetManager(_rootDir), _sceneLoader(_ecsWorld, _assetManager) {}

Application::~Application() {}

void Application::initialize() {
  setHostContext(*this);
  initializeCoreECS();
  loadAndParseManifest();
  registerScriptModule();
  GetModuleRegistry().initializeModules(*this);
  initializeGameFiles();
  loadScenes();

  _eventBus.subscribe<events::Quit>(EVT_AppQuit, [this](const events::Quit& e) {
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
  clearHostContext();
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
      // Deserialize JSON
      [&](World& world, EntityId id, const nlohmann::json& json) {
        // @TODO save json["script"]
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
      },
      // Serialize JSON
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<ScriptComponent>(id);
        if (!comp) {
          return false;
        }

        auto name = comp->name;

        auto instance = _scriptManager.getInstance(comp->instance);
        if (!instance) {
          return false;
        }

        auto scriptHandle = instance->getScriptHandle();
        auto script = _scriptManager.getScript(scriptHandle);
        if (!script) {
          return false;
        }

        // @TODO: Serialize/Deserialize name, it is not kept ATM
        out["script"] = "path/to/script/without/ext";

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        // NO POD DATA FOR SCRIPTS FOR NOW
        return false;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        // NO POD DATA FOR SCRIPTS FOR NOW
        return false;
      });
  _ecsWorld.registerSystem<ScriptSystem>(_scriptManager);

  _scriptManager.initialize(*this);
  _scriptManager.registerHostFunction("__jmLog", {"env", "__jmLog", "v(ii)", &jmLog});
  _scriptManager.registerHostFunction("abort", {"env", "abort", "v(iiii)", &jmAbort});
}

void Application::loadScenes() {
  JM_LOG_INFO("[Scene Loading]: {}", _manifest.entryScene);
  _sceneLoader.loadScene(_manifest.entryScene);
  JM_LOG_INFO("[Scene Loaded]: {}", _sceneLoader.getCurrentSceneName());
}

void Application::initializeCoreECS() {
  _ecsWorld.registerComponent<TransformComponent>(
      // JSON Deserialize
      [&](World& world, EntityId id, const nlohmann::json& json) {
        TransformComponent comp;

        if (json.contains("position") && json["position"].is_array()) {
          std::array<float, 3> posData = json["position"].get<std::array<float, 3>>();
          glm::vec3 position{posData[0], posData[1], posData[2]};
          comp.position = position;
        }

        if (json.contains("scale") && json["scale"].is_array()) {
          std::array<float, 2> scaleData = json["scale"].get<std::array<float, 2>>();
          glm::vec2 scale{scaleData[0], scaleData[1]};
          comp.scale = scale;
        }

        if (json.contains("rotation") && json["rotation"].is_number()) {
          float rotData = json["rotation"].get<float>();
          comp.rotationRad = rotData;
        }
        world.addComponent<TransformComponent>(id, comp);
      },
      // JSON Serialize
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<TransformComponent>(id);
        if (!comp) {
          return false;
        }

        float pos[3] = {0.0f, 0.0f, 0.0f};
        pos[0] = comp->position[0];
        pos[1] = comp->position[1];
        pos[2] = comp->position[2];
        float scale[2] = {0.0f, 0.0f};
        scale[0] = comp->scale[0];
        scale[1] = comp->scale[1];

        out["position"] = pos;
        out["scale"] = scale;
        out["rotation"] = comp->rotationRad;

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODTransformComponent)) return false;

        auto comp = world.getComponent<TransformComponent>(id);
        if (!comp) {
          return false;
        }

        PODTransformComponent pod{};
        std::memcpy(&pod, in.data(), sizeof(pod));

        comp->position[0] = pod.px;
        comp->position[1] = pod.py;
        comp->position[2] = pod.pz;
        comp->scale[0] = pod.sx;
        comp->scale[1] = pod.sy;
        comp->rotationRad = pod.rot;

        return true;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        if (out.size() < sizeof(PODTransformComponent)) return false;

        const auto* comp = world.getComponent<TransformComponent>(id);
        if (!comp) return false;

        PODTransformComponent pod{
            comp->position[0], comp->position[1], comp->position[2],
            comp->scale[0], comp->scale[1],
            comp->rotationRad};

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);
        return true;
      });
}