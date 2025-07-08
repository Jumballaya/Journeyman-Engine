#include "AudioModule.hpp"

#include "../core/app/Registration.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "AudioEmitterComponent.hpp"
#include "AudioHostFunctions.hpp"
#include "AudioSystem.hpp"

REGISTER_MODULE(AudioModule)

void AudioModule::initialize(Application& app) {
  setAudioHostContext(app, *this);

  auto& assetManager = app.getAssetManager();

  app.getWorld().registerSystem<AudioSystem>(_audioManager);
  app.getWorld().registerComponent<AudioEmitterComponent>(
      [&](World& world, EntityId id, const nlohmann::json& json) {
        AudioEmitterComponent emitter;

        if (json.contains("initialSound") && json["initialSound"].is_string()) {
          std::string soundPath = json["initialSound"].get<std::string>();
          AssetHandle assetHandle = assetManager.loadAsset(soundPath);
          const RawAsset& rawAsset = assetManager.getRawAsset(assetHandle);

          // @TODO: add a _audioManager.soundExists(soundPath) and _audioManager.getSoundHandleByName(soundPath);

          AudioHandle audioHandle = _audioManager.registerSound(soundPath, SoundBuffer::decode(rawAsset.data));
          emitter.initialSound = audioHandle;
        }
        if (json.contains("pendingSound") && json["pendingSound"].is_string()) {
          std::string soundPath = json["pendingSound"].get<std::string>();
          AssetHandle assetHandle = assetManager.loadAsset(soundPath);
          const RawAsset& rawAsset = assetManager.getRawAsset(assetHandle);

          // @TODO: add a _audioManager.soundExists(soundPath) and _audioManager.getSoundHandleByName(soundPath);

          AudioHandle audioHandle = _audioManager.registerSound(soundPath, SoundBuffer::decode(rawAsset.data));
          emitter.pendingSound = audioHandle;
        }
        if (json.contains("gain")) {
          emitter.gain = json["gain"].get<float>();
        }
        if (json.contains("looping")) {
          emitter.looping = json["looping"].get<bool>();
        }

        world.addComponent<AudioEmitterComponent>(id, std::move(emitter));
      });

  app.getScriptManager()
      .registerHostFunction("__jmPlaySound", {"env", "__jmPlaySound", "i(iifi)", &playSound});
  app.getScriptManager()
      .registerHostFunction("__jmStopSound", {"env", "__jmStopSound", "v(i)", &stopSound});
  app.getScriptManager()
      .registerHostFunction("__jmFadeOutSound", {"env", "__jmFadeOutSound", "v(if)", &fadeOutSound});
  app.getScriptManager()
      .registerHostFunction("__jmSetGainSound", {"env", "__jmSetGainSound", "v(if)", &setGainSound});

  app.getAssetManager().addAssetConverter({".wav"}, [&](const RawAsset& asset, const AssetHandle& assetHandle) {
    auto buffer = SoundBuffer::decode(asset.data);
    AudioHandle audioHandle = _audioManager.registerSound(asset.filePath.filename().string(), std::move(buffer));
    _handleMap[assetHandle] = audioHandle;
  });

  app.getAssetManager().addAssetConverter({".ogg"}, [&](const RawAsset& asset, const AssetHandle& assetHandle) {
    auto buffer = SoundBuffer::fromFile(asset.filePath);
    AudioHandle audioHandle = _audioManager.registerSound(asset.filePath.filename().string(), std::move(buffer));
    _handleMap[assetHandle] = audioHandle;
  });
}

void AudioModule::shutdown(Application&) {
  clearAudioHostContext();
}

AudioManager& AudioModule::getAudioManager() {
  return _audioManager;
}