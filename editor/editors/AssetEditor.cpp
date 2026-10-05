#include "AssetEditor.hpp"

#include "Project.hpp"

std::unique_ptr<AssetEditor> makeInputBindingsEditor();
std::unique_ptr<AssetEditor> makeTilesetEditor();
std::unique_ptr<AssetEditor> makeAtlasEditor();
std::unique_ptr<AssetEditor> makeDataEditor();
std::unique_ptr<AssetEditor> makeUiEditor();

std::unique_ptr<AssetEditor> makeAssetEditor(const std::string& path) {
  switch (assetKindOf(path)) {
    case AssetKind::Input: return makeInputBindingsEditor();
    case AssetKind::Tileset: return makeTilesetEditor();
    case AssetKind::Atlas: return path.find('#') == std::string::npos ? makeAtlasEditor() : nullptr;
    case AssetKind::Data: return makeDataEditor();
    case AssetKind::Ui: return makeUiEditor();
    default: return nullptr;
  }
}
