// Asset tabs: project files with a dedicated editor, docked beside Scene and Game.

#include <map>

#include <imgui_internal.h>

#include "Editor.hpp"
#include "Entities.hpp"
#include "Icons.hpp"
#include "panels/Panels.hpp"
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
    {"tileset", "New Tileset", "tiles", ".tsj", R"({
 "type": "tileset",
 "version": "1.10",
 "tiledversion": "1.12.2",
 "name": "$NAME",
 "tilewidth": 16,
 "tileheight": 16,
 "columns": 0,
 "margin": 0,
 "spacing": 0,
 "tilecount": 0,
 "grid": {"orientation": "orthogonal", "width": 1, "height": 1},
 "tiles": []
}
)"},
    {"map", "New Tile Map", "level", ".tmj", ""},  // made by newMapText
    {"atlas", "New Atlas", "sprites", ".atlas.json", R"({
  "sources": [],
  "filter": "nearest",
  "padding": 2
}
)"},
    {"data", "New Data Table", "items", ".json", R"({
  "$NAME": [
    { "id": "first", "name": "First" },
    { "id": "second", "name": "Second" }
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

// The open tab for `path`, or null.
template <class Tab>
Tab* tabFor(std::vector<Tab>& tabs, const std::string& path) {
  auto it = std::find_if(tabs.begin(), tabs.end(), [&](const Tab& tab) { return tab.doc->path() == path; });
  return it == tabs.end() ? nullptr : &*it;
}

}  // namespace

void Editor::openSceneAt(const std::string& scene, const std::function<bool(const Json&)>& pick) {
  if (!_project) return;
  openScene(scene);
  if (!_scene || _scene->path() != scene) return;  // waiting on a save prompt: just opens
  for (size_t i = 0; i < _scene->size(); ++i) {
    if (pick(effectiveComponents(*_project, _scene->entity(i)))) {
      select(_scene->uid(i));
      _scenePanel->frameSelection(*this);
      return;
    }
  }
}

std::vector<std::string> Editor::scenesUsingMap(const std::string& path) {
  std::vector<std::string> out;
  if (!_project) return out;
  for (const std::string& scene : _project->scenes()) {
    try {
      for (const Json& e : Json::parse(_project->readText(scene)).at("entities")) {
        if (effectiveComponents(*_project, e).value("TileMapComponent", Json::object()).value("map", Json()) == Json(path)) {
          out.push_back(scene);
          break;
        }
      }
    } catch (const Json::exception&) {
      // A scene that isn't well-formed draws no maps.
    }
  }
  return out;
}

void Editor::addedFile(const std::string& path) {
  if (!_project) return;
  std::string error;
  if (!_project->addToBuild(path, error)) _toasts.show(Toasts::Kind::Error, "Couldn't add it to .jm.json", error);
  writeThrough(path);
  _project->rescan();
}

std::string Editor::freePath(const std::string& folder, std::string typed, const std::string& extension) const {
  std::replace_if(typed.begin(), typed.end(), [](char c) { return c == ' ' || c == '/' || c == '\\'; }, '_');
  if (typed.ends_with(extension)) typed.resize(typed.size() - extension.size());
  std::string path = folder + "/" + typed + extension;
  for (int n = 2; _project->file(path); ++n) path = folder + "/" + typed + "_" + std::to_string(n) + extension;
  return path;
}

std::vector<Editor::NewAssetKind> Editor::newAssetKindsFor(const std::vector<std::string>& types) {
  std::vector<NewAssetKind> out;
  for (const Template& t : kTemplates) {
    if (assetMatches(std::string("new") + t.extension, types)) out.push_back({t.kind, t.title});
  }
  return out;
}

void Editor::newAsset(const std::string& kind, const std::string& folder, std::function<void(const std::string&)> created) {
  if (!_project) return;
  auto t = std::find_if(std::begin(kTemplates), std::end(kTemplates), [&](const Template& t) { return kind == t.kind; });
  if (t == std::end(kTemplates)) return;
  // Unplaced: beside the most files of its kind, else in assets/.
  std::string dir = folder;
  if (dir.empty()) {
    std::map<std::string, int> counts;
    for (const AssetFile& f : _project->files()) {
      if (f.path.ends_with(t->extension)) ++counts[fs::path(f.path).parent_path().generic_string()];
    }
    const auto most = std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    dir = most != counts.end() ? most->first : "assets";
  }
  auto pathFor = [this, t, dir](const std::string& typed) { return freePath(dir, typed, t->extension); };
  prompt(t->title, "Name", t->name, [this, t, pathFor, created = std::move(created)](const std::string& typed) {
    const std::string path = pathFor(typed);
    std::string text = t->text;
    // A table is keyed by what it holds: the file's name.
    if (const size_t at = text.find("$NAME"); at != std::string::npos) text.replace(at, 5, fs::path(path).stem().string());
    if (t->extension == std::string(".tmj")) text = newMapText(path);
    std::string error;
    if (!_project->writeText(path, text, error)) {
      _toasts.show(Toasts::Kind::Error, std::string("Couldn't create ") + fs::path(path).filename().string(), error);
      return;
    }
    addedFile(path);
    if (created) created(path);
    revealAsset(path);
    if (t->extension != std::string(".tmj") || !created) openAsset(path);  // a map made for an entity is already there
  }, pathFor);
}

void Editor::editOpenAsset(const std::string& path, const std::string& label, const std::function<void(Json&)>& change) {
  for (AssetTab& tab : _assetTabs) {
    if (tab.doc->path() == path) tab.doc->edit(label, change);
  }
}

const Json* Editor::tileset(const std::string& path) {
  if (AssetTab* tab = tabFor(_assetTabs, path)) return &tab->doc->value();
  return parsedFile(path);
}

bool Editor::hasAssetEditor(const std::string& path) { return makeAssetEditor(path) != nullptr; }

void Editor::openAsset(const std::string& path, const std::string& item) {
  if (!_project) return;
  if (assetKindOf(path) == AssetKind::Map) {  // painted where a scene draws it: this one, else the first
    auto draws = [&](const Json& c) { return c.value("TileMapComponent", Json::object()).value("map", Json()) == Json(path); };
    bool here = false;
    for (size_t i = 0; _scene && i < _scene->size() && !here; ++i) here = draws(effectiveComponents(*_project, _scene->entity(i)));
    const auto elsewhere = here ? std::vector<std::string>{} : scenesUsingMap(path);
    if (!here && elsewhere.empty()) {
      _toasts.show(Toasts::Kind::Info, "Drag it into a scene to paint it", fs::path(path).filename().string() + " isn't in a scene yet.",
                   "Open in Tiled", [this, path]() { openInTiled(path); });
      return;
    }
    openSceneAt(here ? _scene->path() : elsewhere.front(), draws);
    setTool(Tool::TileBrush);
    focusPanel("Scene");
    return;
  }
  if (AssetTab* tab = tabFor(_assetTabs, path)) {
    tab->focus = true;
    if (!item.empty()) tab->view->show(item);
    return;
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
  if (!item.empty()) view->show(item);
  _assetTabs.push_back({std::move(doc), std::move(view), true});
}

AssetDocument* Editor::activeAsset() {
  AssetTab* tab = tabFor(_assetTabs, _activeAsset);
  return tab ? tab->doc.get() : nullptr;
}

bool Editor::assetCommand(const std::string& id, bool run) {
  AssetTab* tab = tabFor(_assetTabs, _activeAsset);
  if (!tab || !tab->view->handles(id)) return false;
  if (run) tab->view->run(id, *tab->doc);
  return true;
}

bool Editor::drawAssetInspector() {
  AssetTab* tab = tabFor(_assetTabs, _activeAsset);
  return tab && tab->view->drawInspector(*this, *tab->doc);
}

void Editor::saveAssets(bool now) {
  if (!_project) return;
  const bool settled = !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().WantTextInput;
  bool reload = false;
  for (AssetTab& tab : _assetTabs) {
    AssetDocument& doc = *tab.doc;
    // Another program's change comes first, so autosave never writes over it.
    if (doc.reloadIfChanged(*_project) == AssetDocument::DiskChange::ReloadedOverEdits) {
      _toasts.show(Toasts::Kind::Warning, doc.title() + " changed on disk",
                   "Its new version is loaded; Undo brings back your unsaved edits.");
    }
    if (!doc.dirty()) continue;
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
    // Closing saves it now; a save that fails keeps it open, with the error showing.
    if (!open && doc.dirty()) saveAssets(true);
    if (!open && !doc.dirty()) {
      if (_activeAsset == doc.path()) _activeAsset.clear();
      _assetTabs.erase(_assetTabs.begin() + static_cast<long>(i));
      continue;
    }
    ++i;
  }
  // Working in the scene again sends Undo back to it.
  const ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
  for (const char* scenePanel : {"Scene", "Hierarchy", "Game"}) {
    if (nav && nav->RootWindow == ImGui::FindWindowByName(scenePanel)) _activeAsset.clear();
  }
}
