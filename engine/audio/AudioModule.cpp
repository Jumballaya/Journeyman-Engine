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
  app.getWorld().registerComponent<AudioEmitterComponent, PODAudioEmitterComponent>(
      // Deserialize JSON
      [&](World& world, EntityId id, const nlohmann::json& json) {
        AudioEmitterComponent emitter;

        if (json.contains("initialSound") && json["initialSound"].is_string()) {
          // @TODO: Save the name or the asset handle so we can properly serialize back
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
      },
      // JSON Serialize
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<AudioEmitterComponent>(id);
        if (!comp) {
          return false;
        }

        if (!comp->initialSound.has_value()) {
          return false;
        }

        // @TODO: Get the audio name to serialize
        out["gain"] = comp->gain;
        out["looping"] = comp->looping;

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODAudioEmitterComponent)) return false;

        auto comp = world.getComponent<AudioEmitterComponent>(id);
        if (!comp) {
          return false;
        }

        PODAudioEmitterComponent pod{};
        std::memcpy(&pod, in.data(), sizeof(pod));

        comp->gain = pod.gain;
        comp->looping = pod.looping;
        comp->stopRequested = pod.stopRequested;

        return true;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        if (out.size() < sizeof(PODAudioEmitterComponent)) return false;

        const auto* comp = world.getComponent<AudioEmitterComponent>(id);
        if (!comp) return false;

        PODAudioEmitterComponent pod{comp->gain, comp->looping, comp->stopRequested};

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);
        return true;
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

  JM_LOG_INFO("[Audio] initialized");
}

void AudioModule::shutdown(Application&) {
  clearAudioHostContext();
  JM_LOG_INFO("[Audio] shutdown");
}

AudioManager& AudioModule::getAudioManager() {
  return _audioManager;
}