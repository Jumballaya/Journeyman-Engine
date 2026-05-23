#include "UIModule.hpp"

#include <cstdint>
#include <vector>

#include "../app/Engine.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "../core/assets/RawAsset.hpp"
#include "../core/logger/logging.hpp"
#include "Font.hpp"

// Module-init tags live in engine/core/app/ModuleTags.hpp (today only WindowTag
// and OpenGLContextTag; no Renderer2D-init tag). G.1 leaves DependsOn empty and
// relies on add_subdirectory(ui) following renderer2d in engine/CMakeLists.txt
// so Renderer2DModule::initialize runs before UIModule::initialize. G.2/G.5
// revisit when explicit Renderer2D <-> UI init coupling lands; G.7 tightens
// DependsOn to InputsTag.
template <>
struct ModuleTraits<UIModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<>;
};

REGISTER_MODULE(UIModule)

void UIModule::initialize(Engine& app) {
  // The converter lambda captures `this` and lives in AssetManager's converter
  // list for the manager's lifetime. AssetManager runs converters only during
  // loadAsset, never during destruction, and modules are torn down before any
  // shutdown-time loads could fire — so the captured `this` is never
  // dereferenced after UIModule is destroyed. If a future change runs
  // converters on shutdown, capture a stable handle instead.
  auto fontDecoder = [this](const RawAsset& asset, const AssetHandle& handle) {
    if (asset.data.empty()) {
      JM_LOG_ERROR("[Font] empty buffer for '{}'", asset.filePath.string());
      return;
    }
    // .ttc collection rejection: a TrueType Collection starts with the 'ttcf'
    // magic. stbtt_InitFont(index 0) would read only the first face; multi-face
    // support is deferred, so reject explicitly rather than silently truncate.
    if (asset.data.size() >= 4 &&
        asset.data[0] == 't' && asset.data[1] == 't' &&
        asset.data[2] == 'c' && asset.data[3] == 'f') {
      JM_LOG_WARN("[Font] '{}' is a TrueType Collection (.ttc); rejected. "
                  "Multi-face support is deferred.",
                  asset.filePath.string());
      return;
    }
    // RawAsset is const — the converter can't move from asset.data. Copy into a
    // fresh buffer and move THAT into the Font (ttf files are small).
    std::vector<uint8_t> bytes(asset.data.begin(), asset.data.end());
    auto font = Font::tryLoad(std::move(bytes));
    if (!font) {
      JM_LOG_ERROR("[Font] stbtt_InitFont failed for '{}'", asset.filePath.string());
      return;
    }
    _fonts.registerFont(handle, asset.filePath, std::move(font));
  };

  // Register the same lambda for both extension dispatch (folder mode) and the
  // "font" resolver type (archive mode), mirroring the image/atlas converters.
  app.getAssetManager().addAssetConverter({".ttf", ".otf"}, fontDecoder);
  app.getAssetManager().addAssetTypeConverter("font", fontDecoder);
  JM_LOG_INFO("[UI] initialized");
}

void UIModule::shutdown(Engine& /*app*/) {
  // FontRegistry's destructor frees all Fonts via their unique_ptrs. Explicit
  // log for symmetry with other modules.
  JM_LOG_INFO("[UI] shutdown");
}
