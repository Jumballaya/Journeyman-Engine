#pragma once

#include <unordered_map>
#include <utility>

#include "AssetHandle.hpp"

// AssetRegistry<T> is the module-side half of the core↔module asset contract
// (see AssetManager.hpp for the full contract).
//
// Feature modules that decode raw assets into typed forms own one of these,
// keyed by the same AssetHandle that AssetManager issued for the raw bytes.
// A module's ConverterCallback inserts into it; anyone needing the decoded
// form calls get(handle).
//
// Example — an audio module:
//   class AudioModule : public EngineModule {
//     AssetRegistry<AudioBuffer> _audioAssets;
//     void initialize(Engine& engine) override {
//       engine.getAssetManager().addAssetConverter(
//           {".wav", ".ogg"},
//           [this](const RawAsset& raw, const AssetHandle& h) {
//             _audioAssets.insert(h, decodeAudio(raw.data));
//           });
//     }
//   };
//
//   // Elsewhere:
//   AssetHandle music = assets.loadAsset("bgm.ogg");
//   const AudioBuffer* buf = audio.getBuffer(music);  // nullptr on failure
template <typename T>
class AssetRegistry {
 public:
  void insert(AssetHandle h, T value) { _items.insert_or_assign(h, std::move(value)); }

  const T* get(AssetHandle h) const {
    auto it = _items.find(h);
    return it == _items.end() ? nullptr : &it->second;
  }

  T* get(AssetHandle h) {
    auto it = _items.find(h);
    return it == _items.end() ? nullptr : &it->second;
  }

  bool contains(AssetHandle h) const { return _items.count(h) > 0; }

  void erase(AssetHandle h) { _items.erase(h); }

  void clear() { _items.clear(); }

  size_t size() const noexcept { return _items.size(); }

 private:
  std::unordered_map<AssetHandle, T> _items;
};
