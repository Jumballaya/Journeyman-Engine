// The Inspector's view of a project file: a preview (image, atlas regions,
// waveform with playback, font sample, text) and what can be done with it.

#include <filesystem>
#include <map>
#include <memory>

#include <imgui_internal.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"
#include "Ui.hpp"
#include "audio/AudioModule.hpp"
#include "audio/SoundBuffer.hpp"

namespace fs = std::filesystem;

namespace {

std::string sizeText(uintmax_t bytes) {
  char out[32];
  if (bytes < 1024) std::snprintf(out, sizeof(out), "%ju B", bytes);
  else if (bytes < 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f KB", bytes / 1024.0);
  else std::snprintf(out, sizeof(out), "%.1f MB", bytes / (1024.0 * 1024.0));
  return out;
}

// A checkerboard behind transparent pixels.
void checker(ImDrawList* draw, ImVec2 a, ImVec2 b) {
  draw->AddRectFilled(a, b, theme::u32(theme::bg2), theme::radius);
  const float cell = 8.0f;
  draw->PushClipRect(a, b, true);
  for (float y = a.y; y < b.y; y += cell) {
    for (float x = a.x + (static_cast<int>((y - a.y) / cell) % 2 ? cell : 0); x < b.x; x += cell * 2) {
      draw->AddRectFilled({x, y}, {x + cell, y + cell}, theme::u32(theme::bg3));
    }
  }
  draw->PopClipRect();
}

void picturePreview(const Thumbnails::Picture& picture) {
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = std::min(260.0f, std::max(120.0f, width * picture.size.y / std::max(1.0f, picture.size.x)));
  const ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::Dummy({width, height});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  checker(draw, a, {a.x + width, a.y + height});
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
  auto it = fonts.find(file.string());
  if (it != fonts.end()) return it->second;
  ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(file.string().c_str());
  fonts[file.string()] = font;
  return font;
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

}  // namespace

void InspectorPanel::drawAsset(Editor& editor, const std::string& reference) {
  Project& project = *editor.project();
  const std::string path = reference.substr(0, reference.find('#'));
  const AssetFile* file = project.file(path);
  if (!file) {
    ui::emptyState(ICON_FILE_X, "File not found", reference.c_str());
    return;
  }
  const AssetKind kind = reference.find('#') != std::string::npos ? AssetKind::Image : file->kind;
  const AssetKindInfo info = assetKindInfo(file->kind);

  // Header.
  ImGui::PushFont(nullptr, 22.0f);
  ImGui::TextColored(theme::accent, "%s", info.icon);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(reference.substr(reference.find_last_of("/#") + 1).c_str());
  ImGui::PopFont();
  ui::smallText((std::string(info.label) + "   " + sizeText(file->size)).c_str(), theme::textFaint);
  ImGui::EndGroup();
  ui::smallText(path.c_str(), theme::textDim);
  ImGui::Dummy({0, 6});

  const float full = ImGui::GetContentRegionAvail().x;
  switch (kind) {
    case AssetKind::Image: {
      if (auto picture = Thumbnails::instance().get(project, reference)) picturePreview(*picture);
      ImGui::Dummy({0, 4});
      if (editor.scene() && !editor.scene()->isPrefab() && ui::primaryButton(ICON_PLUS "  Add to Scene", {full, 0})) {
        editor.instantiateAsset(reference, editor.scenePanel().viewCenter());
      }
      break;
    }
    case AssetKind::Atlas: {
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
      ImGui::BeginDisabled(!audio);
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
      const size_t count = json.value("entities", Json::array()).size();
      ui::smallText((std::to_string(count) + (count == 1 ? " entity" : " entities")).c_str(), theme::textDim);
      ImGui::Dummy({0, 6});
      if (ui::primaryButton(ICON_FILM_SLATE "  Open Scene", {full, 0})) editor.openScene(path);
      break;
    }
    case AssetKind::Prefab: {
      const Json json = Json::parse(project.readText(path), nullptr, false);
      const std::string image = prefabImage(project, path);
      if (auto picture = image.empty() ? std::nullopt : Thumbnails::instance().get(project, image)) picturePreview(*picture);
      ui::sectionLabel("Components");
      const Json components = json.value("components", Json::object());
      for (const auto& [name, _] : components.items()) {
        ImGui::TextColored(theme::accent, "%s", componentIcon(name));
        ImGui::SameLine(0, 8);
        ImGui::TextUnformatted(componentLabel(name).c_str());
      }
      if (const Json tags = json.value("tags", Json::array()); !tags.empty()) {
        std::string list;
        for (const auto& t : tags) list += (list.empty() ? "#" : "  #") + t.get<std::string>();
        ImGui::Dummy({0, 2});
        ui::smallText(list.c_str(), theme::textDim);
      }
      ImGui::Dummy({0, 6});
      if (ui::primaryButton(ICON_PENCIL_SIMPLE "  Edit Prefab", {full, 0})) editor.editPrefab(path);
      if (editor.scene() && !editor.scene()->isPrefab()) {
        if (ui::button(ICON_PLUS "  Add to Scene", {full, 0})) editor.instantiateAsset(path, editor.scenePanel().viewCenter());
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
    default: {
      if (kind == AssetKind::Script || kind == AssetKind::Ui || kind == AssetKind::Style || kind == AssetKind::Shader ||
          kind == AssetKind::Data || kind == AssetKind::Map || kind == AssetKind::Tileset || file->size < 64 * 1024) {
        if (ui::primaryButton(ICON_CODE "  Open in Editor", {full, 0})) editor.openInCodeEditor(path);
        ImGui::Dummy({0, 4});
        textPreview(project.readText(path));
      }
      break;
    }
  }
  ImGui::Dummy({0, 4});
  if (ui::button(ICON_FOLDER_SIMPLE "  Reveal in File Manager", {full, 0})) editor.revealInFileManager(project.abs(path));
}
