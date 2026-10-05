#include "AssetEditor.hpp"

#include "Project.hpp"

std::unique_ptr<AssetEditor> makeInputBindingsEditor();
std::unique_ptr<AssetEditor> makeTilesetEditor();

std::unique_ptr<AssetEditor> makeAssetEditor(const std::string& path) {
  switch (assetKindOf(path)) {
    case AssetKind::Input: return makeInputBindingsEditor();
    case AssetKind::Tileset: return makeTilesetEditor();
    default: return nullptr;
  }
}
