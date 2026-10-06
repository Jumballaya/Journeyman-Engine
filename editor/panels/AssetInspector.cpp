// The Inspector's view of a project file: a preview (image, atlas regions,
// waveform with playback, font sample, text) and what can be done with it.

#include <filesystem>
#include <map>
#include <memory>

#include <imgui_internal.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "References.hpp"
#include "ScriptInfo.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"
#include "UiThumbnails.hpp"
#include "Ui.hpp"
#include "audio/AudioModule.hpp"
#include "audio/SoundBuffer.hpp"
#include "editors/EditorWidgets.hpp"

namespace fs = std::filesystem;

namespace {

std::string sizeText(uintmax_t bytes) {
  char out[32];
  if (bytes < 1024) std::snprintf(out, sizeof(out), "%ju B", bytes);
  else if (bytes < 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f KB", bytes / 1024.0);
  else std::snprintf(out, sizeof(out), "%.1f MB", bytes / (1024.0 * 1024.0));
  return out;
}

void picturePreview(const Thumbnails::Picture& picture) {
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = std::min(260.0f, std::max(120.0f, width * picture.size.y / std::max(1.0f, picture.size.x)));
  const ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::Dummy({width, height});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  widgets::checker(draw, a, {a.x + width, a.y + height});
  const float fit = std::min((width - 16) / picture.size.x, (height - 16) / picture.size.y);
  const float scale = fit >= 1.0f ? std::floor(fit) : fit;  // whole-pixel enlargement for pixel art
  const ImVec2 s{picture.size.x * scale, picture.size.y * scale};
  const ImVec2 p{std::round(a.x + (width - s.x) * 0.5f), std::round(a.y + (height - s.y) * 0.5f)};
  draw->AddImage(picture.texture, p, {p.x + s.x, p.y + s.y}, picture.uv0, picture.uv1);
  char size[48];
  std::snprintf(size, sizeof(size), "%.0f x %.0f px   %.0f%%", picture.size.x, picture.size.y, scale * 100);
  ui::smallText(size, theme::textFaint);
}

// A sound's waveform (min/max per column) with a playhead while it plays.
struct Waveform {
  std::vector<std::pair<float, float>> columns;
  float seconds = 0;
};

const Waveform* waveformOf(const fs::path& file) {
  static std::map<std::string, std::unique_ptr<Waveform>> cache;
  auto& entry = cache[file.string()];
  if (entry) return entry.get();
  entry = std::make_unique<Waveform>();
  auto buffer = SoundBuffer::fromFile(file);
  if (!buffer || buffer->totalFrames() == 0) return entry.get();
  constexpr size_t kColumns = 160;
  const size_t frames = buffer->totalFrames(), channels = buffer->channels();
  for (size_t c = 0; c < kColumns; ++c) {
    float lo = 0, hi = 0;
    for (size_t f = c * frames / kColumns; f < (c + 1) * frames / kColumns; ++f) {
      const float v = buffer->data()[f * channels];
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    entry->columns.emplace_back(lo, hi);
  }
  entry->seconds = buffer->getDuration();
  return entry.get();
}

ImFont* fontOf(const fs::path& file) {
  // ImGui 1.92 loads fonts at any time; keep one per file.
  static std::map<std::string, ImFont*> fonts;
  auto [it, fresh] = fonts.try_emplace(file.string());
  if (fresh) it->second = ImGui::GetIO().Fonts->AddFontFromFileTTF(file.string().c_str());
  return it->second;
}

void textPreview(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 8});
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::radius);
  ImGui::BeginChild("##text", {0, std::min(360.0f, ImGui::GetContentRegionAvail().y - 8)}, ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 0.5f);
  int lines = 0;
  size_t start = 0;
  while (start < text.size() && lines < 200) {
    const size_t end = std::min(text.find('\n', start), text.size());
    ImGui::TextColored(theme::textFaint, "%3d", ++lines);
    ImGui::SameLine(0, 10);
    ImGui::TextUnformatted(text.data() + start, text.data() + end);
    start = end + 1;
  }
  ImGui::PopFont();
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

// A file naming this one, as a row that's true when clicked.
bool userRow(const std::string& user) {
  ImGui::PushID(user.c_str());
  const bool clicked = ImGui::Selectable((std::string(assetKindInfo(assetKindOf(user)).icon) + "  " + user).c_str());
  ImGui::PopID();
  return clicked;
}

}  // namespace

void InspectorPanel::drawAsset(Editor& editor, const std::string& reference) {
  Project& project = *editor.project();
  const std::string path = reference.substr(0, reference.find('#'));
  const AssetFile* found = project.file(path);
  if (!found) {
    ui::emptyState(ICON_FILE_X, "File not found", reference.c_str());
    return;
  }
  const AssetFile file = *found;  // a copy: the buttons below may rescan the project
  const bool region = reference.find('#') != std::string::npos;
  const AssetKind kind = region ? AssetKind::Image : file.kind;
  const AssetKindInfo info = assetKindInfo(file.kind);

  // Header.
  ImGui::PushFont(nullptr, 22.0f);
  ImGui::TextColored(theme::accent, "%s", info.icon);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(reference.substr(reference.find_last_of("/#") + 1).c_str());
  ImGui::PopFont();
  ui::smallText((std::string(info.label) + "   " + sizeText(file.size)).c_str(), theme::textFaint);
  ImGui::EndGroup();
  ui::smallText(path.c_str(), theme::textDim);
  ImGui::Dummy({0, 6});

  // A file the game can't load: not in the manifest. (Scripts compile from
  // their imports, and images packed into an atlas ship inside it.)
  const bool needsListing = file.kind != AssetKind::Script && file.kind != AssetKind::Other && file.kind != AssetKind::Folder && !region;
  const bool packed = file.kind == AssetKind::Image && std::any_of(project.files().begin(), project.files().end(), [&](const AssetFile& f) {
    return f.kind == AssetKind::Atlas && project.readText(f.path).find("\"" + path + "\"") != std::string::npos;
  });
  if (needsListing && !packed && !project.inBuild(path)) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::withAlpha(theme::warning, 0.10f));
    ImGui::BeginChild("##notInBuild", {0, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextColored(theme::warning, ICON_WARNING "  Not in the build");
    ui::smallText("No entry in .jm.json takes this file, so the game can't load it.", theme::textDim);
    if (ui::button("Add to .jm.json")) editor.addedFile(path);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Dummy({0, 6});
  }

  const float full = ImGui::GetContentRegionAvail().x;
  auto addToScene = [&](bool (*button)(const char*, ImVec2)) {
    if (button(ICON_PLUS "  Add to Scene", {full, 0})) editor.instantiateAsset(reference, editor.scenePanel().viewCenter());
  };
  const bool inScene = editor.scene() && !editor.scene()->isPrefab();
  switch (kind) {
    case AssetKind::Image: {
      if (auto picture = Thumbnails::instance().get(project, reference)) picturePreview(*picture);
      ImGui::Dummy({0, 4});
      if (inScene) addToScene(ui::primaryButton);
      break;
    }
    case AssetKind::Atlas: {
      if (ui::primaryButton(ICON_SQUARES_FOUR "  Edit Atlas", {full, 0})) editor.openAsset(path);
      ImGui::Dummy({0, 2});
      const auto regions = Thumbnails::instance().regions(project, path);
      char count[48];
      std::snprintf(count, sizeof(count), "%zu regions", regions.size());
      ui::sectionLabel(count);
      if (regions.empty()) ui::dimText("Build the project to slice this atlas.");
      const float cell = 48.0f;
      const int columns = std::max(1, static_cast<int>((full + 4) / (cell + 4)));
      for (size_t i = 0; i < regions.size() && i < 240; ++i) {
        if (i % columns) ImGui::SameLine(0, 4);
        const std::string region = path + "#" + regions[i];
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##r", {cell, cell})) editor.inspectAsset(region);
        if (ImGui::BeginDragDropSource()) {
          ImGui::SetDragDropPayload("JM_ASSET", region.data(), region.size());
          ImGui::TextUnformatted(regions[i].c_str());
          ImGui::EndDragDropSource();
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(a, {a.x + cell, a.y + cell}, theme::u32(hovered ? theme::bg3 : theme::bg2), theme::radius);
        if (auto p = Thumbnails::instance().get(project, region)) {
          const float fit = (cell - 6) / std::max(p->size.x, p->size.y);
          const ImVec2 s{p->size.x * fit, p->size.y * fit};
          draw->AddImage(p->texture, {a.x + (cell - s.x) * 0.5f, a.y + (cell - s.y) * 0.5f},
                         {a.x + (cell + s.x) * 0.5f, a.y + (cell + s.y) * 0.5f}, p->uv0, p->uv1);
        }
        if (hovered) ImGui::SetTooltip("%s", regions[i].c_str());
      }
      break;
    }
    case AssetKind::Sound: {
      const Waveform* wave = waveformOf(project.abs(path));
      const float h = 72.0f;
      const ImVec2 a = ImGui::GetCursorScreenPos();
      ImGui::Dummy({full, h});
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(a, {a.x + full, a.y + h}, theme::u32(theme::bg0), theme::radius);
      const float mid = a.y + h * 0.5f;
      const float played = _soundStarted > 0 && wave->seconds > 0
                               ? static_cast<float>((ImGui::GetTime() - _soundStarted) / wave->seconds)
                               : -1.0f;
      for (size_t c = 0; c < wave->columns.size(); ++c) {
        const float x = a.x + 4 + (full - 8) * c / wave->columns.size();
        const auto [lo, hi] = wave->columns[c];
        const bool past = played >= 0 && static_cast<float>(c) / wave->columns.size() <= played;
        draw->AddLine({x, mid - hi * h * 0.45f}, {x, mid - lo * h * 0.45f + 1}, theme::u32(past ? theme::accent : theme::textDim, 0.9f),
                      std::max(1.0f, (full - 8) / wave->columns.size() - 1));
      }
      if (played > 1.0f) _soundStarted = 0;
      char length[32];
      std::snprintf(length, sizeof(length), "%.2f s", wave->seconds);
      ui::smallText(length, theme::textFaint);
      ImGui::Dummy({0, 2});
      HostedEngine* preview = editor.preview().engine();
      AudioModule* audio = preview ? preview->engine().getModules().find<AudioModule>() : nullptr;
      ImGui::BeginDisabled(!audio || wave->seconds <= 0);  // an undecodable sound would never stop "playing"
      if (ui::primaryButton(_soundStarted > 0 ? ICON_STOP "  Stop" : ICON_PLAY "  Play", {full, 0}) && audio) {
        if (_soundStarted > 0) {
          audio->audio().stopAll();
          _soundStarted = 0;
        } else {
          if (!audio->audio().knows(path)) audio->audio().registerSound({path}, SoundBuffer::fromFile(project.abs(path)));
          audio->audio().play(AudioHandle(path));
          _soundStarted = ImGui::GetTime();
        }
      }
      ImGui::EndDisabled();
      break;
    }
    case AssetKind::Font: {
      if (ImFont* font = fontOf(project.abs(path))) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg0);
        ImGui::BeginChild("##font", {0, 180}, ImGuiChildFlags_AlwaysUseWindowPadding);
        for (float size : {12.0f, 18.0f, 28.0f, 40.0f}) {
          ImGui::PushFont(font, size);
          ImGui::TextUnformatted(size < 20 ? "The quick brown fox jumps over the lazy dog. 0123456789" : "Journeyman 1942");
          ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
      }
      break;
    }
    case AssetKind::Scene: {
      const Json json = Json::parse(project.readText(path), nullptr, false);
      const size_t count = json.is_object() ? json.value("entities", Json::array()).size() : 0;  // value() throws on a non-object
      ui::smallText((std::to_string(count) + (count == 1 ? " entity" : " entities")).c_str(), theme::textDim);
      ImGui::Dummy({0, 6});
      if (ui::primaryButton(ICON_FILM_SLATE "  Open Scene", {full, 0})) editor.openScene(path);
      break;
    }
    case AssetKind::Prefab: {
      const Json* prefab = prefabJson(project, path);
      const Json json = prefab ? *prefab : Json::object();
      const std::string image = prefabImage(project, path);
      if (auto picture = image.empty() ? std::nullopt : Thumbnails::instance().get(project, image)) picturePreview(*picture);
      ui::sectionLabel("Components");
      for (const auto& [name, _] : json.value("components", Json::object()).items()) {
        ImGui::TextColored(theme::accent, "%s", componentIcon(name));
        ImGui::SameLine(0, 8);
        ImGui::TextUnformatted(componentLabel(name).c_str());
      }
      if (const Json tags = json.value("tags", Json::array()); !tags.empty()) {
        std::string list;
        for (const auto& t : tags) {
          if (t.is_string()) list += (list.empty() ? "#" : "  #") + t.get<std::string>();
        }
        ImGui::Dummy({0, 2});
        ui::smallText(list.c_str(), theme::textDim);
      }
      ImGui::Dummy({0, 6});
      if (ui::primaryButton(ICON_PENCIL_SIMPLE "  Edit Prefab", {full, 0})) editor.editPrefab(path);
      if (inScene) {
        addToScene(ui::button);
        const auto instances = editor.instancesOf(path);
        ImGui::Dummy({0, 2});
        if (instances.empty()) {
          ui::smallText("Not used in this scene. Drag it into the view to place one.", theme::textFaint);
        } else {
          char text[64];
          std::snprintf(text, sizeof(text), ICON_SELECTION_ALL "  Select %zu in This Scene", instances.size());
          if (ui::button(text, {full, 0})) editor.selectAll(instances);
        }
      }
      break;
    }
    case AssetKind::Script: {
      const ScriptInfo& info = scriptInfo(project, path);
      if (!info.description.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(theme::textDim, "%s", info.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy({0, 4});
      }
      if (ui::primaryButton(ICON_CODE "  Open in Code Editor", {full, 0})) editor.openInCodeEditor(path);
      if (editor.scene()) addToScene(ui::button);
      // What it does: the callbacks it exports.
      static const std::map<std::string, std::pair<const char*, const char*>> kCallbacks = {
          {"onUpdate", {ICON_ARROWS_CLOCKWISE, "Every frame"}},
          {"onMessage", {ICON_CHAT_CIRCLE, "When sent a message"}},
          {"onCollide", {ICON_ARROWS_IN_SIMPLE, "When it touches a collider"}},
          {"onDestroy", {ICON_TRASH, "When it's destroyed"}}};
      ImGui::Dummy({0, 4});
      ui::sectionLabel("Runs");
      ui::smallText("Once when its entity spawns", theme::textDim);
      for (const std::string& c : info.callbacks) {
        auto known = kCallbacks.find(c);
        ImGui::TextColored(theme::accent, "%s", known != kCallbacks.end() ? known->second.first : ICON_LIGHTNING);
        ImGui::SameLine(0, 8);
        ImGui::TextUnformatted(known != kCallbacks.end() ? known->second.second : c.c_str());
        ImGui::SameLine(0, 8);
        ImGui::PushFont(theme::fonts().mono, theme::sizeSmall);
        ImGui::TextColored(theme::textFaint, "%s", c.c_str());
        ImGui::PopFont();
      }
      ImGui::Dummy({0, 4});
      ui::sectionLabel("Params");
      if (info.params.empty()) ui::smallText("Reads none. Params.number(\"speed\", 100) in the script adds one to the Inspector.", theme::textFaint);
      for (const ScriptInfo::Param& p : info.params) {
        ImGui::TextColored(theme::textFaint, "%s", p.number ? ICON_HASH : ICON_TEXT_T);
        ImGui::SameLine(0, 8);
        ImGui::TextUnformatted(p.key.c_str());
        ImGui::SameLine(0, 8);
        char number[32];
        if (p.number) std::snprintf(number, sizeof(number), "%g", p.fallback.get<double>());
        ImGui::TextColored(theme::textFaint, "default %s", p.number ? number : ("\"" + p.fallback.get<std::string>() + "\"").c_str());
      }
      ImGui::Dummy({0, 4});
      const auto users = scriptUsers(project, path);
      ui::sectionLabel(users.empty() ? "Not used yet" : "Used in");
      for (const std::string& u : users) {
        if (!userRow(u)) continue;
        if (assetKindOf(u) == AssetKind::Prefab) editor.editPrefab(u);
        else editor.openSceneAt(u, [&](const Json& c) { return c.value("ScriptComponent", Json::object()).value("script", std::string()) == path; });
      }
      break;
    }
    case AssetKind::Map: {
      const auto scenes = editor.scenesUsingMap(path);
      if (!scenes.empty()) {
        if (ui::primaryButton(ICON_PAINT_BRUSH "  Paint in the Scene", {full, 0})) {
          editor.openSceneAt(scenes.front(), [&](const Json& c) { return c.value("TileMapComponent", Json::object()).value("map", Json()) == Json(path); });
          editor.setTool(Tool::TileBrush);
        }
        ui::smallText(("Drawn in " + scenes.front() + (scenes.size() > 1 ? " and " + std::to_string(scenes.size() - 1) + " more" : "")).c_str(), theme::textFaint);
      } else {
        ui::smallText("No scene draws this map yet: drag it into a scene.", theme::textFaint);
      }
      ImGui::Dummy({0, 2});
      if (ui::button(ICON_ARROW_SQUARE_OUT "  Open in Tiled", {full, 0})) editor.openInTiled(path);
      break;
    }
    default: {
      if (kind == AssetKind::Ui) {
        if (auto picture = UiThumbnails::instance().get(project, path, editor.buildGeneration())) picturePreview(*picture);
        ImGui::Dummy({0, 4});
      }
      if (Editor::hasAssetEditor(path)) {
        if (ui::primaryButton((std::string(info.icon) + "  Edit " + info.label).c_str(), {full, 0})) editor.openAsset(path);
        ImGui::Dummy({0, 2});
        if (ui::button(ICON_CODE "  Open as Text", {full, 0})) editor.openInCodeEditor(path);
        break;
      }
      if (kind == AssetKind::Ui || kind == AssetKind::Style || kind == AssetKind::Shader || kind == AssetKind::Data ||
          file.size < 64 * 1024) {
        if (ui::primaryButton(ICON_CODE "  Open in Editor", {full, 0})) editor.openInCodeEditor(path);
        ImGui::Dummy({0, 4});
        textPreview(project.readText(path));
      }
      break;
    }
  }
  // Where it's used (scripts list their users above).
  if (kind != AssetKind::Script && kind != AssetKind::Folder && !region) {
    const auto users = referencesTo(project, path);
    ImGui::Dummy({0, 6});
    ui::sectionLabel(users.empty() ? "Not used by name anywhere" : ("Used in " + std::to_string(users.size()) + (users.size() == 1 ? " file" : " files")).c_str());
    for (size_t i = 0; i < users.size() && i < 12; ++i) {
      const std::string& u = users[i];
      if (!userRow(u)) continue;
      const AssetKind k = assetKindOf(u);
      if (k == AssetKind::Scene) editor.openScene(u);
      else if (k == AssetKind::Prefab) editor.editPrefab(u);
      else if (k == AssetKind::Script) editor.openInCodeEditor(u);
      else if (u == ".jm.json") editor.commands().run("project.settings");
      else editor.openAsset(u);
    }
    if (users.size() > 12) ui::smallText(("and " + std::to_string(users.size() - 12) + " more").c_str(), theme::textFaint);
  }
  ImGui::Dummy({0, 4});
  if (ui::button(ICON_FOLDER_SIMPLE "  Reveal in File Manager", {full, 0})) editor.revealInFileManager(project.abs(path));
}
