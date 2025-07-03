#include "AudioModule.hpp"

#include "../core/app/Registration.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "AudioHostFunctions.hpp"
#include "AudioSystem.hpp"

REGISTER_MODULE(AudioModule)

void AudioModule::initialize(Application& app) {
  setAudioHostContext(app, *this);

  app.getWorld().registerSystem<AudioSystem>(_audioManager);

  app.getScriptManager()
      .registerHostFunction({"env", "playSound", "v(ii)", &playSound});

  app.getAssetManager().addAssetConverter({".ogg", ".wav"}, [&](const RawAsset& asset, const AssetHandle& assetHandle) {
    auto buffer = SoundBuffer::decode(asset.data);
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