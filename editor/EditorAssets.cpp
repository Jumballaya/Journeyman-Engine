// Asset tabs: project files with a dedicated editor, docked beside Scene and Game.

#include <imgui_internal.h>

#include "Editor.hpp"
#include "Icons.hpp"
#include "editors/AssetEditor.hpp"

namespace fs = std::filesystem;

namespace {

constexpr double kSettleSeconds = 0.5;  // autosave waits this long after the last change

// What "New" makes of each kind: a default name, an extension, and a starting point.
struct Template {
  const char* kind;
  const char* title;  // for the name prompt
  const char* name;
  const char* extension;
  const char* text;
};

constexpr Template kTemplates[] = {
    {"script", "New Script", "new_script", ".ts", R"(import { Input, Params, self } from "@jm/runtime";

// Runs once, when the entity spawns. Params come from the Inspector.
const speed = <f32>Params.number("speed", 100);
const me = self();

// Runs every frame.
export function onUpdate(dt: f32): void {
  me.transform.x += Input.axis("left", "right") * speed * dt;
  me.transform.y += Input.axis("down", "up") * speed * dt;
}
)"},
    {"effect", "New Post Effect", "new_effect", ".frag", R"(// A post effect: runs for every pixel of the finished frame.
// u_strength gets a slider in the editor; scripts set it with setFloat.
uniform float u_strength;

void main() {
  vec4 color = texture(u_primary, v_texCoord);
  float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
  outColor = vec4(mix(color.rgb, vec3(gray), u_strength), color.a);
}
)"},
    {"transition", "New Transition", "new_transition", ".frag", R"(// A scene transition: u_progress runs from 0 (the old scene, u_aux)
// to 1 (the new one, u_primary). Use it with Scene.transition(scene, 1, "name").
void main() {
  vec2 local = (gl_FragCoord.xy - u_viewport.xy) / u_viewport.zw;  // 0..1 over the game
  float edge = smoothstep(u_progress - 0.08, u_progress + 0.08, local.x);
  outColor = mix(texture(u_primary, v_texCoord), texture(u_aux, v_texCoord), edge);
}
)"},
    {"stylesheet", "New Stylesheet", "styles", ".css", R"(/* Shared UI styles: link it from a screen with
   <link rel="stylesheet" href="assets/ui/styles.css"> */
.panel { background-color: #000000cc; border: 2px solid #ffffff; padding: 6px; }
.title { font-size: 16px; color: #ffffff; }
)"},
    {"tileset", "New Tileset", "new", ".tileset.json", R"({
  "atlas": "",
  "tiles": {
    ".": {},
    "#": { "solid": true }
  }
}
)"},
    {"atlas", "New Atlas", "sprites", ".atlas.json", R"({
  "sources": [],
  "filter": "nearest",
  "padding": 2
}
)"},
    {"data", "New Data Table", "items", ".json", R"({
  "items": [
    { "name": "Potion", "price": 10 },
    { "name": "Ether", "price": 25 }
  ]
}
)"},
    {"input", "New Input Actions", "input", ".bindings.json", R"({
  "actions": {
    "left": ["ArrowLeft", "A", "Gamepad.DPadLeft", "Gamepad.LeftStickLeft"],
    "right": ["ArrowRight", "D", "Gamepad.DPadRight", "Gamepad.LeftStickRight"],
    "up": ["ArrowUp", "W", "Gamepad.DPadUp", "Gamepad.LeftStickUp"],
    "down": ["ArrowDown", "S", "Gamepad.DPadDown", "Gamepad.LeftStickDown"],
    "confirm": ["Enter", "Space", "Gamepad.A"],
    "back": ["Escape", "Gamepad.B"]
  }
}
)"},
    {"ui", "New UI Screen", "screen", ".ui.html", R"(<style>
  #root { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 8px; }
  .title { font-size: 16px; color: #ffffff; }
</style>
<div id="root">
  <div class="title">New Screen</div>
</div>
)"},
};

}  // namespace

void Editor::addedFile(const std::string& path) {
  if (!_project) return;
  std::string error;
  if (!_project->addToBuild(path, error)) _toasts.show(Toasts::Kind::Error, "Couldn't add it to .jm.json", error);
  writeThrough(path);
  _project->rescan();
}

void Editor::newAsset(const std::string& kind, const std::string& folder) {
  if (!_project) return;
  auto t = std::find_if(std::begin(kTemplates), std::end(kTemplates), [&](const Template& t) { return kind == t.kind; });
  if (t == std::end(kTemplates)) return;
  prompt(t->title, "Name", t->name, [this, t, folder](const std::string& typed) {
    std::string name = typed;
    for (char& c : name) {
      if (c == ' ' || c == '/' || c == '\\') c = '_';
    }
    if (name.ends_with(t->extension)) name.resize(name.size() - std::strlen(t->extension));
    std::string path = folder + "/" + name + t->extension;
    for (int n = 2; _project->file(path); ++n) path = folder + "/" + name + "_" + std::to_string(n) + t->extension;
    std::string error;
    if (!_project->writeText(path, t->text, error)) {
      _toasts.show(Toasts::Kind::Error, std::string("Couldn't create ") + fs::path(path).filename().string(), error);
      return;
    }
    addedFile(path);
    revealAsset(path);
    openAsset(path);
  });
}

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

bool Editor::assetCommand(const std::string& id, bool run) {
  for (AssetTab& tab : _assetTabs) {
    if (tab.doc->path() != _activeAsset || !tab.view->handles(id)) continue;
    if (run) tab.view->run(id, *tab.doc);
    return true;
  }
  return false;
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
