#include "AudioModule.hpp"

#include <string>

#include "../core/app/Engine.hpp"
#include "../core/app/Registration.hpp"
#include "../core/logger/logging.hpp"
#include "AudioEmitterComponent.hpp"
#include "AudioSystem.hpp"
#include "SoundBuffer.hpp"

REGISTER_MODULE(AudioModule)

namespace {

AudioBus busNamed(const std::string& name) {
  return name == "music" ? AudioBus::Music : AudioBus::Sfx;
}

}  // namespace

void AudioModule::initialize(Engine& app) {
  // One decoder for every format: miniaudio sniffs the bytes.
  auto decode = [this](const RawAsset& asset, const AssetHandle&) {
    const auto& path = asset.filePath;
    _audio.registerSound({path.lexically_normal().generic_string(), path.filename().string(), path.stem().string()},
                         SoundBuffer::decode(asset.data));
  };
  app.getAssetManager().addAssetConverter({".wav", ".ogg", ".mp3", ".flac"}, decode);
  app.getAssetManager().addAssetTypeConverter("audio", decode);

  app.getWorld().registerComponent<AudioEmitterComponent>({
      .fromJson = [this, &app](AudioEmitterComponent& c, const nlohmann::json& json, EntityId) {
        const std::string sound = json.value("sound", std::string());
        if (!_audio.knows(sound) && sound.find('/') != std::string::npos) {
          try {
            app.getAssetManager().loadAsset(sound);
          } catch (const std::exception& e) {
            JM_LOG_ERROR("[Audio] sound '{}' failed to load: {}", sound, e.what());
          }
        }
        if (!_audio.knows(sound)) JM_LOG_ERROR("[Audio] unknown sound '{}'", sound);
        c.sound = AudioHandle(sound);
        c.gain = json.value("gain", c.gain);
        c.looping = json.value("looping", c.looping);
        c.bus = busNamed(json.value("bus", std::string("sfx")));
      },
      .onDestroy = [this](AudioEmitterComponent& c) {
        if (c.playing) _audio.fade(c.playing, 0.3f);
      },
      .schema = {"Audio Emitter", "Audio", "Plays a sound when the entity spawns",
                 {FieldSchema::asset("sound", {".wav", ".ogg", ".mp3", ".flac"}, "The sound file"),
                  FieldSchema::number("gain", 1, "Volume", 0, 2, 0.01f),
                  FieldSchema::boolean("looping", false, "Loop until the entity is destroyed"),
                  FieldSchema::choice("bus", {"sfx", "music"}, "Mixer bus")}},
  });
  app.getWorld().registerSystem<AudioSystem>(_audio);

  // The outgoing scene's sounds fade before the next scene starts its own.
  app.getSceneManager().addUnloadListener([this]() { _audio.fadeOutAll(0.25f); });

  bindScriptApi(app);
  JM_LOG_INFO("[Audio] initialized");
}

void AudioModule::shutdown(Engine&) {
  JM_LOG_INFO("[Audio] shutdown");
}

void AudioModule::bindScriptApi(Engine& app) {
  ScriptManager& s = app.getScriptManager();
  s.bind("__jmSoundPlay", [this](std::string name, float gain, bool loop, int32_t bus) -> uint32_t {
    const SoundInstanceId id = _audio.play(AudioHandle(name), gain, loop,
                                           bus == static_cast<int32_t>(AudioBus::Music) ? AudioBus::Music : AudioBus::Sfx);
    if (id == 0) JM_LOG_WARN("[Audio] unknown sound '{}'", name);
    return id;
  });
  s.bind("__jmSoundStop", [this](uint32_t id) { _audio.stop(id); });
  s.bind("__jmSoundFadeOut", [this](uint32_t id, float seconds) { _audio.fade(id, seconds); });
  s.bind("__jmSoundSetGain", [this](uint32_t id, float gain) { _audio.setGain(id, gain); });
  s.bind("__jmAudioSetBusVolume", [this](int32_t bus, float volume) {
    if (bus >= 0 && bus < static_cast<int32_t>(AudioBus::Count)) _audio.setBusVolume(static_cast<AudioBus>(bus), volume);
  });
  s.bind("__jmAudioStopAll", [this](float fadeSeconds) { _audio.fadeOutAll(fadeSeconds); });
}
