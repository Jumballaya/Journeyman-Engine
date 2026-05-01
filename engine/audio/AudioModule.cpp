#include "AudioModule.hpp"

#include <optional>

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/Registration.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "../core/logger/logging.hpp"
#include "AudioEmitterComponent.hpp"
#include "AudioHostFunctions.hpp"
#include "AudioSystem.hpp"

REGISTER_MODULE(AudioModule)

void AudioModule::initialize(Engine& app) {
  setAudioHostContext(app, *this);

  auto& assetManager = app.getAssetManager();

  app.getWorld().registerSystem<AudioSystem>(_audioManager);
  app.getWorld().registerComponent<AudioEmitterComponent, PODAudioEmitterComponent>(
      // Deserialize JSON
      [&](World& world, EntityId id, const nlohmann::json& json) {
        AudioEmitterComponent emitter;

        // Resolve name → AssetHandle → AudioHandle via the registry.
        // loadAsset is idempotent (dedupes by path), so repeated references
        // are free whether the sound was preloaded or first-seen here.
        auto resolveSound = [&](const std::string& soundPath) -> std::optional<AudioHandle> {
          try {
            AssetHandle assetHandle = assetManager.loadAsset(soundPath);
            const AudioHandle* audioHandle = _audio.get(assetHandle);
            if (audioHandle) return *audioHandle;
            JM_LOG_ERROR("[AudioEmitter] sound decode missing for: {}", soundPath);
          } catch (const std::exception& e) {
            JM_LOG_ERROR("[AudioEmitter] sound load failed for '{}': {}", soundPath, e.what());
          }
          return std::nullopt;
        };

        if (json.contains("initialSound") && json["initialSound"].is_string()) {
          if (auto h = resolveSound(json["initialSound"].get<std::string>())) {
            emitter.initialSound = *h;
          }
        }
        if (json.contains("pendingSound") && json["pendingSound"].is_string()) {
          if (auto h = resolveSound(json["pendingSound"].get<std::string>())) {
            emitter.pendingSound = *h;
          }
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

        // @TODO(asset-path-roundtrip): source sound path not retained on the
        // component. See AssetManager.hpp "Known limitation".
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

  // Single decoder for both .wav and .ogg: miniaudio's ma_decoder_init_memory
  // (used by SoundBuffer::decode) autodetects format from the byte stream.
  // The earlier .ogg-via-fromFile path was a bug — it tried to open
  // asset.filePath from disk, which fails in archive mode where the path is a
  // resolver key, not a real filesystem path.
  auto audioDecoder = [&](const RawAsset& asset, const AssetHandle& assetHandle) {
    auto buffer = SoundBuffer::decode(asset.data);
    AudioHandle audioHandle = _audioManager.registerSound(asset.filePath.filename().string(), std::move(buffer));
    _audio.insert(assetHandle, audioHandle);
  };
  app.getAssetManager().addAssetConverter({".wav"}, audioDecoder);
  app.getAssetManager().addAssetConverter({".ogg"}, audioDecoder);
  app.getAssetManager().addAssetTypeConverter("audio", audioDecoder);

  _eventBus = &app.getEventBus();
  _sceneUnloadSub = _eventBus->subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [this](const events::SceneUnloading&) { _audioManager.stopAll(); });

  JM_LOG_INFO("[Audio] initialized");
}

void AudioModule::shutdown(Engine&) {
  if (_eventBus && _sceneUnloadSub) {
    _eventBus->unsubscribe(_sceneUnloadSub);
    _sceneUnloadSub = 0;
    _eventBus = nullptr;
  }
  clearAudioHostContext();
  JM_LOG_INFO("[Audio] shutdown");
}

AudioManager& AudioModule::getAudioManager() {
  return _audioManager;
}