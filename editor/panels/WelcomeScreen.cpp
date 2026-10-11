#include <chrono>
#include <filesystem>

#include "FolderPicker.hpp"

#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "core/app/Platform.hpp"

#include <imgui_stdlib.h>

namespace fs = std::filesystem;

namespace {

std::string ago(int64_t unixSeconds) {
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const int64_t s = std::max<int64_t>(0, now - unixSeconds);
  if (s < 60) return "just now";
  if (s < 3600) return std::to_string(s / 60) + " min ago";
  const auto count = [](int64_t n, const char* unit) { return std::to_string(n) + " " + unit + (n == 1 ? "" : "s") + " ago"; };
  if (s < 86400) return count(s / 3600, "hour");
  if (s < 86400 * 30) return count(s / 86400, "day");
  return count(s / (86400 * 30), "month");
}

// Example projects shipped beside the editor (the repo's demos/).
std::vector<fs::path> exampleFolders() {
  std::vector<fs::path> out;
  const fs::path here = platform::executableDir();
  for (const fs::path& dir : {here / "demos", here / ".." / "demos", here / ".." / ".." / "demos", here / ".." / ".." / ".." / "demos"}) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (fs::exists(entry.path() / ".jm.json")) out.push_back(fs::weakly_canonical(entry.path()));
    }
    std::sort(out.begin(), out.end());
    break;
  }
  return out;
}

// `text` fit to `width` by cutting its middle: a path keeps its start and its folder.
std::string middleEllipsis(const std::string& text, float width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
  const auto continues = [&](size_t i) { return i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80; };
  std::string out;
  for (size_t keep = text.size(); keep-- > 0;) {
    size_t head = keep / 2, tail = text.size() - (keep - keep / 2);
    while (continues(head)) --head;  // whole UTF-8 characters only
    while (continues(tail)) ++tail;
    out = text.substr(0, head) + "..." + text.substr(tail);
    if (ImGui::CalcTextSize(out.c_str()).x <= width) break;
  }
  return out;
}

// A wide, flat row button: icon, title, subtitle, and right-aligned detail.
bool rowButton(const char* id, const char* icon, const std::string& title, const std::string& subtitle,
               const std::string& detail, bool dim) {
  const float width = ImGui::GetContentRegionAvail().x;
  const float h = 52.0f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(id, {width, h});
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (hovered) draw->AddRectFilled(pos, {pos.x + width, pos.y + h}, theme::u32(theme::bg3), theme::radiusOverlay);
  ImGui::PushFont(nullptr, 20.0f);
  const ImVec2 is = ImGui::CalcTextSize(icon);
  draw->AddText({pos.x + 14, pos.y + (h - is.y) * 0.5f}, theme::u32(dim ? theme::textFaint : theme::accent), icon);
  ImGui::PopFont();
  ImGui::PushFont(theme::fonts().semibold, 0.0f);
  draw->AddText({pos.x + 48, pos.y + 9}, theme::u32(dim ? theme::textFaint : theme::text), title.c_str());
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  draw->AddText({pos.x + 48, pos.y + 29}, theme::u32(theme::textFaint), middleEllipsis(subtitle, width - 160).c_str());
  const ImVec2 ds = ImGui::CalcTextSize(detail.c_str());
  draw->AddText({pos.x + width - ds.x - 16, pos.y + (h - ds.y) * 0.5f}, theme::u32(theme::textFaint), detail.c_str());
  ImGui::PopFont();
  return pressed;
}

}  // namespace

void WelcomeScreen::openNewProject(int from) {
  _newRequested = true;
  _newFrom = from;
  _newName = from < 0 ? "my-game" : _examples[from].folder.filename().string();
  if (_newFolder.empty()) _newFolder = gamesFolder().string();
}

void WelcomeScreen::drawNewProject(Editor& editor) {
  if (std::exchange(_newRequested, false)) ImGui::OpenPopup("New Project");
  ui::centerNextWindow({520, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22, 20});
  const bool open = ImGui::BeginPopupModal("New Project", nullptr,
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar();
  if (!open) return;
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle + 2);
  ImGui::TextUnformatted(ICON_PLUS "  New Project");
  ImGui::PopFont();
  ImGui::Dummy({0, 6});
  if (ui::beginProperties("new", 110)) {
    ui::propertyRow("Name", "The game's folder name, and its name until you change it");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##name", &_newName);
    ui::propertyRow("In", "The folder it goes in");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##folder", &_newFolder);
    ImGui::SameLine();
    if (ui::button("Choose...", {82, 0})) {
      const FolderPick pick = pickFolder(_newFolder);
      if (!pick.path.empty()) _newFolder = pick.path.string();
    }
    ui::propertyRow("Start from", "Empty, or a copy of an example (yours to change; the example stays as it was)");
    if (ui::beginCombo("##from", _newFrom < 0 ? "Empty" : _examples[_newFrom].name.c_str())) {
      if (ImGui::Selectable("Empty", _newFrom < 0)) _newFrom = -1;
      for (int i = 0; i < static_cast<int>(_examples.size()); ++i)
        if (ImGui::Selectable(_examples[i].name.c_str(), _newFrom == i)) _newFrom = i;
      ImGui::EndCombo();
    }
    ui::endProperties();
  }
  const bool named = !_newName.empty() && _newName[0] != '-' && _newName[0] != '.' &&
                     _newName.find_first_of("/\\") == std::string::npos;
  ImGui::Dummy({0, 4});
  if (named) ui::dimText(("Makes " + ui::displayPath((fs::path(_newFolder) / _newName).string())).c_str());
  else ImGui::TextColored(theme::warning, "Give it a name (no slashes, not starting with - or .).");
  ImGui::Dummy({0, 10});
  const float w = 96;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2 * w - 8);
  if (ui::button("Cancel", {w, 32}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::SameLine(0, 8);
  const bool ready = named && !editor.cli().busy();
  ImGui::BeginDisabled(!ready);
  if (ui::primaryButton("Create", {w, 32}) || (ready && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
    create(editor);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

void WelcomeScreen::create(Editor& editor) {
  const fs::path dir = fs::path(_newFolder) / _newName;
  std::error_code ec;
  if (_newFrom < 0 && fs::exists(dir / ".jm.json", ec)) {
    editor.openProject(dir);  // already a game: open it (a copy never opens what's there)
    return;
  }
  if (_newFrom >= 0) {
    if (copyProject(_examples[_newFrom].folder, dir, _newName, _error)) editor.openProject(dir);
    return;
  }
  if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) {
    _error = dir.string() + " already exists and isn't empty.";
    return;
  }
  fs::create_directories(dir, ec);
  if (ec) {
    _error = "Couldn't make " + dir.string() + ": " + ec.message();
    return;
  }
  _error.clear();
  _creating = dir;
  editor.cli().start(dir, {"init", _newName}, "New Project");
}

void WelcomeScreen::draw(Editor& editor) {
  if (ImGui::GetTime() - _loadedAt > 2.0) {  // recents change when projects open elsewhere
    _loadedAt = ImGui::GetTime();
    _recents = recentProjects();
    if (_examples.empty()) {
      for (const fs::path& dir : exampleFolders()) {
        std::string error;
        const auto project = Project::open(dir, error);
        _examples.push_back({dir, project ? project->name() : dir.filename().string()});
      }
    }
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg0);
  ImGui::Begin("##welcome", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();

  const float contentWidth = std::min(980.0f, vp->Size.x - 80.0f);
  const float left = (vp->Size.x - contentWidth) * 0.5f;
  const float top = std::max(48.0f, vp->Size.y * 0.12f);
  const float columnGap = 56.0f;
  const float leftWidth = contentWidth * 0.36f;
  const float rightWidth = contentWidth - leftWidth - columnGap;

  // Left: identity and the two ways in.
  ImGui::SetCursorPos({left, top});
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeDisplay + 6);
  ImGui::TextColored(theme::accent, ICON_COMPASS_ROSE);
  ImGui::SameLine(0, 12);
  ImGui::TextUnformatted("Journeyman");
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeTitle);
  ImGui::TextColored(theme::textDim, "A 2D game editor");
  ImGui::PopFont();
  ImGui::Dummy({0, 28});

  const float bw = std::min(leftWidth, 280.0f);
  if (ui::primaryButton(ICON_FOLDER_OPEN "  Open Project", {bw, 40})) {
    const FolderPick pick = pickFolder();
    if (!pick.error.empty()) _error = "Couldn't show the folder dialog: " + pick.error;
    else if (!pick.path.empty() && !editor.openProject(pick.path)) _error = "That folder has no .jm.json.";
  }
  ImGui::Dummy({0, 4});
  if (ui::button(ICON_PLUS "  New Project", {bw, 40})) openNewProject(-1);
  if (!_error.empty()) {
    ImGui::Dummy({0, 6});
    ImGui::PushTextWrapPos(left + bw);
    ImGui::TextColored(theme::error, ICON_WARNING_CIRCLE "  %s", _error.c_str());
    ImGui::PopTextWrapPos();
  }
  if (editor.cli().busy() && !_creating.empty()) {
    ImGui::Dummy({0, 8});
    ui::spinner(8, theme::u32(theme::accent));
    ImGui::SameLine();
    ui::dimText("Creating project...");
  }

  ImGui::Dummy({0, 36});
  ui::sectionLabel("Shortcuts", bw);
  ImGui::PushFont(nullptr, theme::sizeSmall);
  for (const auto& [label, chord] : std::vector<std::pair<const char*, ImGuiKeyChord>>{
           {"Command palette", ImGuiMod_Ctrl | ImGuiKey_K}, {"Open project", ImGuiMod_Ctrl | ImGuiKey_O},
           {"Play", ImGuiMod_Ctrl | ImGuiKey_P}, {"All shortcuts", ImGuiMod_Ctrl | ImGuiKey_Slash}}) {
    ImGui::TextColored(theme::textDim, "%s", label);
    const std::string keys = shortcutLabel(chord);
    ImGui::SameLine(bw - ImGui::CalcTextSize(keys.c_str()).x);
    ImGui::TextColored(theme::textFaint, "%s", keys.c_str());
  }
  ImGui::PopFont();
  ImGui::Dummy({0, 24});
  ui::sectionLabel("Learn", bw);
  if (ImGui::TextLink(ICON_ROCKET_LAUNCH "  Getting started")) editor.openUrl("https://jumballaya.github.io/Journeyman-Engine/start/");
  if (ImGui::TextLink(ICON_BOOK_OPEN "  The editor guide")) editor.openUrl("https://jumballaya.github.io/Journeyman-Engine/docs/editor/");
  ImGui::EndGroup();

  // Right: recent projects, then examples.
  ImGui::SetCursorPos({left + leftWidth + columnGap, top + 6});
  ImGui::BeginChild("##recents", {rightWidth, vp->Size.y - top - 40}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted("Recent projects");
  ImGui::PopFont();
  if (!_recents.empty()) {
    ImGui::SameLine(rightWidth - 200);
    ui::searchField("recentSearch", _filter, "Filter", 200);
  }
  ImGui::Dummy({0, 6});
  if (_recents.empty()) {
    ImGui::Dummy({0, 6});
    ui::dimText("Projects you open appear here.");
  }
  for (const RecentProject& r : _recents) {
    if (!_filter.empty() && ui::fuzzyScore(r.name + " " + r.path, _filter) < 0) continue;
    const bool missing = !fs::exists(fs::path(r.path) / ".jm.json");
    ImGui::PushID(r.path.c_str());
    bool open = rowButton("##row", missing ? ICON_FOLDER_DASHED : ICON_FOLDER_SIMPLE, r.name, ui::displayPath(r.path),
                          missing ? std::string("Missing") : ago(r.opened), missing);
    if (open && missing) _error = r.path + " has moved or been deleted.";
    if (ImGui::BeginPopupContextItem("row menu")) {
      open |= ImGui::MenuItem(ICON_FOLDER_OPEN "  Open", nullptr, false, !missing);
      if (ImGui::MenuItem(ICON_ARROW_SQUARE_OUT "  Reveal in File Manager", nullptr, false, !missing)) {
        editor.revealInFileManager(fs::path(r.path) / ".jm.json");
      }
      ImGui::Separator();
      if (ImGui::MenuItem(ICON_X "  Remove from List")) {
        forgetProject(r.path);
        _loadedAt = -100;
      }
      ImGui::EndPopup();
    }
    if (open && !missing && !editor.openProject(r.path)) _error = "Couldn't open " + r.path;
    ImGui::PopID();
  }

  if (!_examples.empty()) {
    ImGui::Dummy({0, 18});
    ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
    ImGui::TextUnformatted("Examples");
    ImGui::PopFont();
    ImGui::Dummy({0, 6});
    ui::dimText("Opening one makes your own copy to change.");
    ImGui::Dummy({0, 4});
    for (int i = 0; i < static_cast<int>(_examples.size()); ++i) {
      const Example& example = _examples[i];
      ImGui::PushID(example.folder.string().c_str());
      if (rowButton("##example", ICON_GAME_CONTROLLER, example.name, ui::displayPath(example.folder.string()), "Example", false)) {
        openNewProject(i);
      }
      if (ImGui::BeginPopupContextItem("example menu")) {
        if (ImGui::MenuItem(ICON_COPY "  Make a Copy...")) openNewProject(i);
        if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Open the Original")) editor.openProject(example.folder);
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  drawNewProject(editor);

  // A new project opens once `jm init` finishes (a failed one has its own toast).
  if (!_creating.empty() && !editor.cli().busy()) {
    const fs::path dir = std::exchange(_creating, {});
    if (fs::exists(dir / ".jm.json")) editor.openProject(dir);
  }
  ImGui::End();
}
