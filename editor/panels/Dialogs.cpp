// Modal dialogs: Export Game and Project Settings.

#include <cmath>
#include <filesystem>
#include <functional>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <nfd.hpp>

#include "CliRunner.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace fs = std::filesystem;

namespace {

struct Target {
  const char* id;     // jm's --target
  const char* label;  // shown
  const char* icon;
  const char* exe;    // the player's file name
};

constexpr Target kTargets[] = {
    {"darwin-arm64", "macOS (Apple Silicon)", ICON_APPLE_LOGO, "journeyman_engine"},
    {"darwin-amd64", "macOS (Intel)", ICON_APPLE_LOGO, "journeyman_engine"},
    {"windows-amd64", "Windows", ICON_WINDOWS_LOGO, "journeyman_engine.exe"},
    {"linux-amd64", "Linux", ICON_LINUX_LOGO, "journeyman_engine"},
};

std::string hostTarget() {
#if defined(__APPLE__) && defined(__aarch64__)
  return "darwin-arm64";
#elif defined(__APPLE__)
  return "darwin-amd64";
#elif defined(_WIN32)
  return "windows-amd64";
#else
  return "linux-amd64";
#endif
}

// Where jm looks for other platforms' players (export.go: findPlayer).
std::optional<fs::path> findPlayer(const Target& target) {
  std::vector<fs::path> dirs;
  if (const char* env = std::getenv("JM_PLAYERS")) dirs.push_back(env);
  if (const fs::path jm = CliRunner::locate(); !jm.empty()) {
    dirs.push_back(jm.parent_path() / "players");
    dirs.push_back(jm.parent_path() / ".." / "players");
  }
  for (const fs::path& dir : dirs) {
    if (fs::exists(dir / target.id / target.exe)) return dir / target.id / target.exe;
  }
  return std::nullopt;
}

// Left: label; right: control filling the rest.
void formRow(const char* label, const char* hint = nullptr) { ui::propertyRow(label, hint); }

bool dialogButtons(const char* primary, bool primaryEnabled, bool& cancel) {
  ImGui::Dummy({0, 8});
  ImGui::Separator();
  ImGui::Dummy({0, 6});
  const float bw = 110.0f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw * 2 - 8);
  cancel = ui::button("Cancel", {bw, 0}) || ui::dismissPressed();
  ImGui::SameLine(0, 8);
  ImGui::BeginDisabled(!primaryEnabled);
  const bool ok = ui::primaryButton(primary, {bw, 0});
  ImGui::EndDisabled();
  return ok;
}

}  // namespace

void ExportDialog::draw(Editor& editor) {
  if (_open) {
    ImGui::OpenPopup("Export Game");
    _open = false;
    const std::string host = hostTarget();
    for (int i = 0; i < 4; ++i) {
      if (host == kTargets[i].id) _target = i;
    }
  }
  ui::centerNextWindow({580, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 20});
  if (!ImGui::BeginPopupModal("Export Game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                          ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PopStyleVar();
    return;
  }
  Project& project = *editor.project();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle + 2);
  ImGui::TextUnformatted(ICON_PACKAGE "  Export Game");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 536);
  ui::dimText("Builds one executable with the engine and every asset inside. Players need nothing else installed.");
  ImGui::PopTextWrapPos();
  ImGui::Dummy({0, 10});

  // Target: one card per platform.
  ui::sectionLabel("Platform");
  const std::string host = hostTarget();
  for (int i = 0; i < 4; ++i) {
    const Target& t = kTargets[i];
    const bool isHost = host == t.id;
    const bool ready = isHost || findPlayer(t).has_value();
    if (i % 2) ImGui::SameLine(0, 8);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = 264.0f, h = 52.0f;
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##target", {w, h})) _target = i;
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool selected = _target == i;
    draw->AddRectFilled(p, {p.x + w, p.y + h}, theme::u32(selected ? theme::bg3 : hovered ? theme::bg3 : theme::bg1), theme::radiusOverlay);
    if (selected) draw->AddRect(p, {p.x + w, p.y + h}, theme::u32(theme::accent), theme::radiusOverlay, 1.5f);
    ImGui::PushFont(nullptr, 20.0f);
    draw->AddText({p.x + 14, p.y + 14}, theme::u32(selected ? theme::accent : theme::textDim), t.icon);
    ImGui::PopFont();
    ImGui::PushFont(theme::fonts().medium, 0.0f);
    draw->AddText({p.x + 46, p.y + 8}, theme::u32(theme::text), t.label);
    ImGui::PopFont();
    ImGui::PushFont(nullptr, theme::sizeSmall);
    draw->AddText({p.x + 46, p.y + 28}, theme::u32(ready ? (isHost ? theme::success : theme::textDim) : theme::warning),
                  isHost ? ICON_CHECK " This computer" : ready ? ICON_CHECK " Player found" : ICON_WARNING " Needs a player build");
    ImGui::PopFont();
  }
  const Target& target = kTargets[_target];
  const bool isHost = host == target.id;
  const bool ready = isHost || findPlayer(target).has_value();
  if (!ready) {
    ImGui::Dummy({0, 4});
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 536);
    ImGui::TextColored(theme::warning, ICON_INFO "  Exporting for %s needs the engine built on that platform. "
                                       "Run the \"players\" CI workflow and put %s in players/%s/ beside jm.",
                       target.label, target.exe, target.id);
    ImGui::PopTextWrapPos();
  }

  ImGui::Dummy({0, 10});
  ui::sectionLabel("Output");
  if (ui::beginProperties("export", 120)) {
    formRow("Folder", "Inside the project unless absolute");
    const float browse = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse - 4);
    ImGui::InputText("##out", &_out);
    ImGui::SameLine(0, 4);
    if (ui::iconButton("browse", ICON_FOLDER_OPEN, "Choose a folder")) {
      NFD::UniquePath folder;
      if (NFD::PickFolder(folder, project.root().string().c_str()) == NFD_OKAY) _out = folder.get();
    }
    if (std::string(target.id).starts_with("darwin")) {
      formRow("Format", "An .app opens with a double-click; the bare binary runs from a terminal");
      bool app = !_bare;
      if (ui::toggle("##app", &app)) _bare = !app;
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ui::dimText(app ? "Application bundle (.app)" : "Single executable file");
    }
    ui::endProperties();
  }
  // What goes in.
  ImGui::Dummy({0, 6});
  const auto scenes = project.scenes();
  size_t assets = 0;
  for (const AssetFile& f : project.files()) assets += f.kind != AssetKind::Folder;
  char summary[160];
  std::snprintf(summary, sizeof(summary), ICON_GAME_CONTROLLER "  %s %s   ·   %zu scenes   ·   %zu files",
                project.name().c_str(), project.manifest().value("version", std::string("")).c_str(), scenes.size(), assets);
  ui::smallText(summary, theme::textDim);

  bool cancel = false;
  if (dialogButtons(ICON_PACKAGE "  Export", ready, cancel)) {
    std::vector<std::string> args = {"--target", target.id};
    if (_bare) args.push_back("--bare");
    if (!isHost) {
      args.push_back("--player");
      args.push_back(findPlayer(target)->string());
    }
    editor.exportGame(args, _out);
    ImGui::CloseCurrentPopup();
  }
  if (cancel) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  ImGui::PopStyleVar();
}

void SettingsDialog::draw(Editor& editor) {
  Project* project = editor.project();
  if (_open) {
    ImGui::OpenPopup("Project Settings");
    _open = false;
    _draft = project->manifest();
  }
  ui::centerNextWindow({780, 600});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  if (!ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
    ImGui::PopStyleVar();
    return;
  }
  ImGui::PopStyleVar();

  // Section list.
  struct Section {
    const char* icon;
    const char* label;
  };
  static constexpr Section kSections[] = {{ICON_INFO, "General"}, {ICON_STACK, "Content"}, {ICON_MONITOR, "Display"}, {ICON_TEXT_AA, "Interface"},
                                          {ICON_PACKAGE, "Export"}};
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 18});
  ImGui::BeginChild("##sections", {180, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted("  Settings");
  ImGui::PopFont();
  ImGui::Dummy({0, 8});
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.0f, 0.5f});
  for (int i = 0; i < static_cast<int>(std::size(kSections)); ++i) {
    const std::string item = std::string(kSections[i].icon) + "  " + kSections[i].label;
    if (ImGui::Selectable(item.c_str(), _section == i, 0, {0, 30})) _section = i;
  }
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 0);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 18});
  ImGui::BeginChild("##form", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  Json& config = _draft["config"];
  if (!config.is_object()) config = Json::object();
  auto object = [](Json& parent, const char* key) -> Json& {
    Json& o = parent[key];
    if (!o.is_object()) o = Json::object();
    return o;
  };
  auto text = [](Json& o, const char* key, const char* label, const char* hint) {
    formRow(label, hint);
    std::string v = o.value(key, std::string());
    if (ImGui::InputText((std::string("##") + key).c_str(), &v)) o[key] = v;
  };
  auto integer = [](Json& o, const char* key, const char* label, int fallback, const char* hint) {
    formRow(label, hint);
    int v = o.value(key, fallback);
    if (ImGui::InputInt((std::string("##") + key).c_str(), &v, 0)) o[key] = std::max(0, v);
  };
  auto boolean = [](Json& o, const char* key, const char* label, bool fallback, const char* hint) {
    formRow(label, hint);
    bool v = o.value(key, fallback);
    if (ui::toggle((std::string("##") + key).c_str(), &v)) o[key] = v;
  };
  auto color = [](Json& o, const char* key, const char* label, const char* hint) {
    formRow(label, hint);
    float c[4] = {0, 0, 0, 1};
    const Json v = o.value(key, Json::array({0, 0, 0, 1}));
    for (int i = 0; i < 4 && i < static_cast<int>(v.size()); ++i) c[i] = v[i].get<float>();
    // A swatch and a hex code, as designers read colors; the manifest keeps 0..1 floats.
    if (ui::colorField(key, c)) {
      auto round = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
      o[key] = {round(c[0]), round(c[1]), round(c[2]), round(c[3])};
    }
  };

  const float formHeight = ImGui::GetContentRegionAvail().y - 64;
  ImGui::BeginChild("##fields", {0, formHeight});
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(kSections[_section].label);
  ImGui::PopFont();
  ImGui::Dummy({0, 8});
  if (ui::beginProperties("settings", 170)) {
    switch (_section) {
      case 0: {
        text(_draft, "name", "Name", "The game's name: window title, save folder, exported file");
        text(_draft, "version", "Version", nullptr);
        formRow("First scene", "Where the game starts");
        const std::string entry = _draft.value("entryScene", std::string());
        if (ui::beginCombo("##entry", entry.c_str())) {
          for (const std::string& s : project->scenes()) {
            if (ImGui::Selectable(s.c_str(), s == entry)) _draft["entryScene"] = s;
          }
          ImGui::EndCombo();
        }
        break;
      }
      case 1: {
        ui::endProperties();
        contentSection(*project);
        ui::beginProperties("settings", 170);
        break;
      }
      case 2: {
        Json& window = object(config, "window");
        Json& renderer = object(config, "renderer");
        integer(window, "width", "Window width", 1280, "Points");
        integer(window, "height", "Window height", 720, "Points");
        boolean(window, "resizable", "Resizable", true, nullptr);
        boolean(window, "fullscreen", "Start fullscreen", false, nullptr);
        boolean(window, "vsync", "VSync", true, "Match the display's refresh rate");
        boolean(window, "hideCursor", "Hide cursor", false, nullptr);
        integer(renderer, "logicalWidth", "Game width", 0, "The resolution the game draws at; 0 = the window's");
        integer(renderer, "logicalHeight", "Game height", 0, nullptr);
        color(renderer, "clearColor", "Background", "Behind everything in the game");
        color(renderer, "letterboxColor", "Letterbox", "The bars when the window's shape differs from the game's");
        break;
      }
      case 3: {
        Json& uiConfig = object(config, "ui");
        fileChoice(*project, uiConfig, "defaultFont", "Default font", "Text in UI documents; none = the built-in pixel font",
                   {AssetKind::Font}, "Built-in pixel font");
        break;
      }
      case 4: {
        Json& exportConfig = object(config, "export");
        fileChoice(*project, exportConfig, "icon", "App icon", "A PNG in the project (macOS)", {AssetKind::Image}, "None");
        text(exportConfig, "bundleId", "Bundle ID", "com.studio.game (macOS)");
        break;
      }
    }
    ui::endProperties();
  }
  ImGui::EndChild();
  bool cancel = false;
  // Sections create empty objects as they're shown; those aren't changes.
  std::function<void(Json&)> prune = [&](Json& v) {
    if (!v.is_object()) return;
    for (auto it = v.begin(); it != v.end();) {
      prune(*it);
      it = it->is_object() && it->empty() ? v.erase(it) : std::next(it);
    }
  };
  Json changed = _draft, saved = project->manifest();
  prune(changed);
  prune(saved);
  if (dialogButtons("Save", changed != saved, cancel)) {
    project->manifest() = changed;
    std::string error;
    if (project->saveManifest(error)) {
      editor.toasts().show(Toasts::Kind::Success, "Project settings saved", "The project rebuilds with them now.");
    } else {
      editor.toasts().show(Toasts::Kind::Error, "Couldn't save settings", error);
    }
    ImGui::CloseCurrentPopup();
  }
  if (cancel) ImGui::CloseCurrentPopup();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::EndPopup();
}

void SettingsDialog::fileChoice(const Project& project, Json& object, const char* key, const char* label, const char* hint,
                                std::vector<AssetKind> kinds, const char* none) {
  formRow(label, hint);
  const std::string current = object.value(key, std::string());
  if (ui::beginCombo((std::string("##") + key).c_str(), current.empty() ? none : current.c_str())) {
    if (ImGui::Selectable(none, current.empty())) object.erase(key);
    for (const AssetFile& f : project.files()) {
      if (std::find(kinds.begin(), kinds.end(), f.kind) == kinds.end()) continue;
      if (ImGui::Selectable(f.path.c_str(), f.path == current)) object[key] = f.path;
    }
    ImGui::EndCombo();
  }
}

void SettingsDialog::contentSection(const Project& project) {
  // Scenes, in the order the manifest lists them; the first scene is marked.
  ui::sectionLabel("Scenes the game can load");
  Json& scenes = _draft["scenes"];
  if (!scenes.is_array()) scenes = Json::array();
  const std::string entry = _draft.value("entryScene", std::string());
  std::optional<std::pair<size_t, int>> move;
  std::optional<size_t> drop;
  for (size_t i = 0; i < scenes.size(); ++i) {
    if (!scenes[i].is_string()) continue;
    const std::string s = scenes[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(s == entry ? theme::accent : theme::textFaint, s == entry ? ICON_FLAG : ICON_FILM_SLATE);
    if (s == entry) ui::tooltip("The first scene");
    ImGui::SameLine(0, 8);
    ImGui::TextColored(project.file(s) ? theme::text : theme::error, "%s", s.c_str());
    if (!project.file(s)) ui::tooltip("Missing: the file isn't in the project");
    const float buttons = 3 * (ImGui::GetFrameHeight() + 2);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - buttons);
    ImGui::BeginDisabled(i == 0);
    if (ui::iconButton("up", ICON_ARROW_UP, "Move up")) move = {i, -1};
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(i + 1 == scenes.size());
    if (ui::iconButton("down", ICON_ARROW_DOWN, "Move down")) move = {i, 1};
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    if (ui::iconButton("remove", ICON_X, "Leave it out of the game")) drop = i;
    ImGui::PopID();
  }
  if (move) std::swap(scenes[move->first], scenes[move->first + move->second]);
  if (drop) scenes.erase(*drop);
  for (const std::string& s : project.scenes()) {
    if (manifestTakes(_draft, s)) continue;
    ImGui::PushID(s.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textFaint, ICON_FILM_SLATE "  %s", s.c_str());
    ImGui::SameLine();
    ImGui::TextColored(theme::warning, "not in the game");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60);
    if (ui::button("Add", {60, 0})) scenes.push_back(s);
    ImGui::PopID();
  }

  // Asset entries: paths or globs ("assets/sounds/*.wav"), each with what it takes now.
  ImGui::Dummy({0, 8});
  ui::sectionLabel("Files the game ships with");
  Json& assets = _draft["assets"];
  if (!assets.is_array()) assets = Json::array();
  std::optional<size_t> dropAsset;
  for (size_t i = 0; i < assets.size(); ++i) {
    if (!assets[i].is_string()) continue;
    const std::string pattern = assets[i];
    size_t matches = 0;
    for (const AssetFile& f : project.files()) {
      if (f.kind != AssetKind::Folder && manifestEntryMatches(pattern, f.path)) ++matches;
    }
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 0.5f);
    ImGui::TextUnformatted(pattern.c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, 10);
    ImGui::TextColored(matches ? theme::textFaint : theme::warning, matches == 1 ? "1 file" : "%zu files", matches);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
    if (ui::iconButton("remove", ICON_X, "Remove the entry")) dropAsset = i;
    ImGui::PopID();
  }
  if (dropAsset) assets.erase(*dropAsset);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
  const bool entered = ImGui::InputTextWithHint("##newEntry", "assets/sounds/*.wav  (* within a folder, ** across folders)", &_newEntry,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine(0, 4);
  if ((ui::button("Add", {66, 0}) || entered) && !_newEntry.empty()) {
    assets.push_back(_newEntry);
    _newEntry.clear();
  }

  // What nothing takes: files the game would fail to load if it asked for them.
  std::vector<std::string> left;
  for (const AssetFile& f : project.files()) {
    const AssetKind k = f.kind;
    if (k == AssetKind::Folder || k == AssetKind::Other || k == AssetKind::Script || k == AssetKind::Scene || k == AssetKind::Image) continue;
    if (!manifestTakes(_draft, f.path)) left.push_back(f.path);
  }
  if (!left.empty()) {
    ImGui::Dummy({0, 8});
    ui::sectionLabel("Left out");
    for (const std::string& f : left) {
      ImGui::PushID(f.c_str());
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(theme::warning, "%s", assetKindInfo(assetKindOf(f)).icon);
      ImGui::SameLine(0, 8);
      ImGui::TextUnformatted(f.c_str());
      ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60);
      if (ui::button("Add", {60, 0})) assets.push_back(f);
      ImGui::PopID();
    }
  }
}
