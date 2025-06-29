#include "ScriptingModule.hpp"

#include "../core/app/Application.hpp"
#include "../core/app/Registration.hpp"
#include "ScriptComponent.hpp"
#include "ScriptSystem.hpp"

REGISTER_MODULE(ScriptingModule)

void ScriptingModule::initialize(Application& app) {
  auto& world = app.getWorld();
  auto& assetManager = app.getAssetManager();

  std::cout << "[Scripting Module] initializing\n";

  world.registerComponent<ScriptComponent>(
      [&](World& world, EntityId id, const nlohmann::json& json) {
        std::string manifestPath = json["script"].get<std::string>() + ".script.json";

        AssetHandle manifestHandle = assetManager.loadAsset(manifestPath);
        const RawAsset& manifestAsset = assetManager.getRawAsset(manifestHandle);
        nlohmann::json manifestJson = nlohmann::json::parse(std::string(
            manifestAsset.data.begin(),
            manifestAsset.data.end()));

        std::string wasmPath = manifestJson["binary"].get<std::string>();
        AssetHandle wasmHandle = assetManager.loadAsset(wasmPath);
        const RawAsset& wasmAsset = assetManager.getRawAsset(wasmHandle);

        ScriptHandle scriptHandle = _scriptManager.loadScript(wasmAsset.data);
        ScriptInstanceHandle instanceHandle = _scriptManager.createInstance(scriptHandle);

        world.addComponent<ScriptComponent>(id, instanceHandle);
      });

  world.registerSystem<ScriptSystem>(_scriptManager);
}

void ScriptingModule::shutdown(Application&) {}