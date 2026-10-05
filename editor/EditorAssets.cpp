// Asset tabs: project files with a dedicated editor, docked beside Scene and Game.

#include <imgui_internal.h>

#include "Editor.hpp"
#include "editors/AssetEditor.hpp"

namespace {

constexpr double kSettleSeconds = 0.5;  // autosave waits this long after the last change

}  // namespace

bool Editor::hasAssetEditor(const std::string& path) { return makeAssetEditor(path) != nullptr; }

void Editor::openAsset(const std::string& path) {
  if (!_project) return;
  for (AssetTab& tab : _assetTabs) {
    if (tab.doc->path() == path) {
      tab.focus = true;
      return;
    }
  }
  auto view = makeAssetEditor(path);
  if (!view) {
    openInCodeEditor(path);
    return;
  }
  std::string error;
  auto doc = AssetDocument::load(*_project, path, error);
  if (!doc) {
    _toasts.show(Toasts::Kind::Error, "Couldn't open " + std::filesystem::path(path).filename().string(), error,
                 "Open as Text", [this, path]() { openInCodeEditor(path); });
    return;
  }
  _assetTabs.push_back({std::move(doc), std::move(view), true});
}

AssetDocument* Editor::activeAsset() {
  for (AssetTab& tab : _assetTabs) {
    if (tab.doc->path() == _activeAsset) return tab.doc.get();
  }
  return nullptr;
}

bool Editor::drawAssetInspector() {
  for (AssetTab& tab : _assetTabs) {
    if (tab.doc->path() == _activeAsset) return tab.view->drawInspector(*this, *tab.doc);
  }
  return false;
}

void Editor::saveAssets(bool now) {
  if (!_project) return;
  const bool settled = !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().WantTextInput;
  bool reload = false;
  for (AssetTab& tab : _assetTabs) {
    AssetDocument& doc = *tab.doc;
    if (!doc.dirty()) {
      doc.reloadIfChanged(*_project);
      continue;
    }
    if (!now && (!settled || ImGui::GetTime() - doc.changedAt() < kSettleSeconds)) continue;
    std::string error;
    if (!doc.save(*_project, error)) {
      _toasts.show(Toasts::Kind::Error, "Couldn't save " + doc.title(), error);
      continue;
    }
    writeThrough(doc.path());
    reload = true;
  }
  // The preview loaded the old file; a restart picks up the saved one (packed
  // atlases and scripts wait for the rebuild their change triggers).
  if (reload) {
    restartPreview();
    _preview.invalidate();
  }
}

void Editor::drawAssetTabs() {
  // New tabs join the Scene view's dock node.
  ImGuiWindow* sceneWindow = ImGui::FindWindowByName("Scene");
  const ImGuiID centerDock = sceneWindow ? sceneWindow->DockId : 0;
  for (size_t i = 0; i < _assetTabs.size();) {
    AssetTab& tab = _assetTabs[i];
    AssetDocument& doc = *tab.doc;
    const std::string name = std::string(assetKindInfo(assetKindOf(doc.path())).icon) + "  " + doc.title() +
                             (doc.dirty() ? "  \xE2\x80\xA2" : "") + "###asset:" + doc.path();
    if (centerDock) ImGui::SetNextWindowDockID(centerDock, ImGuiCond_FirstUseEver);
    if (tab.focus) {
      ImGui::SetNextWindowFocus();
      tab.focus = false;
      _activeAsset = doc.path();
    }
    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool visible = ImGui::Begin(name.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (visible) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) _activeAsset = doc.path();
      tab.view->draw(*this, doc);
    }
    ImGui::End();
    if (!open) {
      std::string error;
      if (doc.dirty() && doc.save(*_project, error)) writeThrough(doc.path());
      if (_activeAsset == doc.path()) _activeAsset.clear();
      _assetTabs.erase(_assetTabs.begin() + static_cast<long>(i));
      continue;
    }
    ++i;
  }
  // Working in the scene again sends Undo back to it.
  for (const char* scenePanel : {"Scene", "Hierarchy", "Game"}) {
    ImGuiWindow* w = ImGui::FindWindowByName(scenePanel);
    if (w && ImGui::GetCurrentContext()->NavWindow && ImGui::GetCurrentContext()->NavWindow->RootWindow == w) _activeAsset.clear();
  }
}
