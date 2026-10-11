// The Game view: the running game, scaled to fit (or to whole pixels), with
// keyboard focus following the view.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "HostedEngine.hpp"

#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

// The pointer over the game goes to it, as a window's would: position in frame pixels, buttons
// pressed over it (released wherever they're let go), and the wheel.
void GamePanel::forwardMouse(Editor& editor, ImVec2 at, bool overGame) {
  HostedEngine* game = editor.game();
  if (!game) return;
  const ImGuiIO& io = ImGui::GetIO();
  const float fb = io.DisplayFramebufferScale.x;
  const ImVec2 pointer{(io.MousePos.x - at.x) * fb, (io.MousePos.y - at.y) * fb};
  const bool moved = pointer.x != _pointer.x || pointer.y != _pointer.y;
  // Only when it moves: a paused game's events wait in a queue that would fill.
  if (moved && (overGame || std::any_of(std::begin(_buttonsDown), std::end(_buttonsDown), [](bool b) { return b; }))) {
    game->mouseMove(pointer.x, pointer.y);
    _pointer = pointer;
  }
  for (int b = 0; b < 3; ++b) {
    if (overGame && ImGui::IsMouseClicked(b)) {
      game->mouseButton(b, true);
      _buttonsDown[b] = true;
    }
    if (_buttonsDown[b] && ImGui::IsMouseReleased(b)) {
      game->mouseButton(b, false);
      _buttonsDown[b] = false;
    }
  }
  if (overGame && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) game->mouseWheel(-io.MouseWheelH, io.MouseWheel);
}

void GamePanel::draw(Editor& editor, float dt) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  ImDrawList* draw = ImGui::GetWindowDrawList();

  if (!editor.playing()) {
    editor.setGameFocused(false);
    std::ranges::fill(_buttonsDown, false);  // a stopped game hears no releases
    _pointer = {-1.0f, -1.0f};  // the next game hasn't been told
    if (editor.playPending()) {
      const ImVec2 c{origin.x + avail.x * 0.5f, origin.y + avail.y * 0.45f};
      ImGui::SetCursorScreenPos({c.x - 12, c.y - 30});
      ui::spinner(12, theme::u32(theme::accent));
      const char* text = "Building, then the game starts";
      ImGui::SetCursorScreenPos({c.x - ImGui::CalcTextSize(text).x * 0.5f, c.y + 6});
      ui::dimText(text);
      return;
    }
    const std::string hint = "Runs the open scene as the game sees it. " + shortcutLabel(ImGuiMod_Ctrl | ImGuiKey_P) + " plays and stops.";
    if (ui::emptyState(ICON_GAME_CONTROLLER, "The game isn't running", hint.c_str(), ICON_PLAY "  Play")) editor.startPlay();
    return;
  }

  const float barHeight = 30.0f;
  ImGui::SetCursorScreenPos({origin.x + 8, origin.y + 4});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 3});
  for (int mode = 0; mode < 2; ++mode) {
    const char* label = mode == 0 ? "Fit" : "Pixel Perfect";
    ImGui::PushStyleColor(ImGuiCol_Button, _scaleMode == mode ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, _scaleMode == mode ? theme::text : theme::textDim);
    if (ImGui::Button(label)) _scaleMode = mode;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
  }
  ImGui::PushStyleColor(ImGuiCol_Button, _profiling ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, _profiling ? theme::text : theme::textDim);
  if (ImGui::Button("Profile")) _profiling = !_profiling;
  ImGui::SetItemTooltip("Where the game's frames go: each system, script and module (ms average, worst), entities and draw calls");
  ImGui::PopStyleColor(2);
  ImGui::SameLine();
  ImGui::PopStyleVar(2);
  if (HostedEngine* game = editor.game()) game->engine().setProfiling(_profiling);  // a new game starts without
  const glm::ivec2 logical = editor.preview().gameSize();
  ImGui::SameLine(0, 14);
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::TextColored(theme::textFaint, "%d x %d   %.0f fps", logical.x, logical.y, ImGui::GetIO().Framerate);
  ImGui::SameLine(0, 14);
  if (editor.gameHasKeyboard()) ImGui::TextColored(theme::accent, ICON_KEYBOARD "  Game has the keyboard");
  else ImGui::TextColored(theme::textFaint, ICON_CURSOR_CLICK "  Click the game to control it");
  ImGui::PopFont();

  // The view: the whole area, or the largest whole multiple of the game's size.
  const ImVec2 area{avail.x, avail.y - barHeight};
  const ImVec2 areaOrigin{origin.x, origin.y + barHeight};
  ImVec2 size = area;
  if (_scaleMode == 1 && logical.x > 0 && logical.y > 0) {
    const float scale = std::max(1.0f, std::floor(std::min(area.x / logical.x, area.y / logical.y)));
    size = {logical.x * scale, logical.y * scale};
  }
  const ImVec2 at{std::floor(areaOrigin.x + (area.x - size.x) * 0.5f), std::floor(areaOrigin.y + (area.y - size.y) * 0.5f)};
  const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
  const unsigned texture = editor.advanceGame(static_cast<int>(size.x * fb), static_cast<int>(size.y * fb), dt);
  if (!editor.playing()) return;  // the game quit this frame

  ImGui::SetCursorScreenPos(at);
  ImGui::InvisibleButton("##game", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool overGame = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::SetWindowFocus();
  forwardMouse(editor, at, overGame);
  draw->AddImage(static_cast<ImTextureID>(texture), at, {at.x + size.x, at.y + size.y}, {0, 1}, {1, 0});
  const bool focused = ImGui::IsWindowFocused();
  editor.setGameFocused(focused);
  draw->AddRect({at.x - 1, at.y - 1}, {at.x + size.x + 1, at.y + size.y + 1},
                theme::u32(theme::accent, focused ? 0.9f : (editor.paused() ? 0.5f : 0.25f)), 0.0f, 2.0f);
  if (auto outline = liveOutline(editor, at, size)) {
    draw->PushClipRect(at, {at.x + size.x, at.y + size.y}, true);
    draw->AddPolyline(outline->data(), 4, theme::u32(theme::accent), 2.0f, ImDrawFlags_Closed);
    draw->PopClipRect();
  }
  if (_profiling) drawProfile(editor, at, size);
  if (editor.paused()) {
    const char* text = ICON_PAUSE "  Paused";
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 p{at.x + (size.x - ts.x) * 0.5f - 12, at.y + 14};
    draw->AddRectFilled(p, {p.x + ts.x + 24, p.y + ts.y + 12}, theme::u32(theme::bg0, 0.85f), theme::radiusOverlay);
    draw->AddText({p.x + 12, p.y + 6}, theme::u32(theme::accent), text);
  }
}

void GamePanel::drawProfile(Editor& editor, ImVec2 at, ImVec2 size) {
  Profiler* profiler = editor.game() ? editor.game()->engine().profiler() : nullptr;
  if (!profiler) return;
  if (_profileRows.empty() || ++_profileAge >= 30) {
    _profileAge = 0;
    _profileRows.clear();
    const nlohmann::json r = profiler->report();
    char value[64];
    const auto timing = [&](const nlohmann::json& t) {
      std::snprintf(value, sizeof(value), "%.2f / %.2f ms", t["avgMs"].get<double>(), t["worstMs"].get<double>());
      return std::string(value);
    };
    _profileRows.push_back({"Frame (average / worst)", timing(r["frame"]), true});
    for (const char* kind : {"systems", "scripts", "modules"}) {
      if (!r[kind].empty()) _profileRows.push_back({kind, "", true});
      for (size_t i = 0; i < std::min<size_t>(r[kind].size(), 4); ++i)
        _profileRows.push_back({"  " + r[kind][i]["name"].get<std::string>(), timing(r[kind][i]), false});
    }
    for (const auto& [name, count] : r["counts"].items()) {
      std::snprintf(value, sizeof(value), "%.0f", count["avg"].get<double>());
      _profileRows.push_back({name, value, false});
    }
  }
  ImDrawList* draw = ImGui::GetWindowDrawList();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  float labels = 0.0f, values = 0.0f;
  for (const ProfileRow& row : _profileRows) {
    labels = std::max(labels, ImGui::CalcTextSize(row.label.c_str()).x);
    values = std::max(values, ImGui::CalcTextSize(row.value.c_str()).x);
  }
  const float lineHeight = ImGui::GetTextLineHeight(), width = labels + 16 + values;
  const ImVec2 p{at.x + size.x - width - 20, at.y + 8};
  draw->AddRectFilled(p, {p.x + width + 12, p.y + lineHeight * _profileRows.size() + 12}, theme::u32(theme::bg0, 0.85f),
                      theme::radiusOverlay);
  for (size_t i = 0; i < _profileRows.size(); ++i) {
    const ProfileRow& row = _profileRows[i];
    const float y = p.y + 6 + lineHeight * i;
    const ImU32 color = theme::u32(row.heading ? theme::accent : theme::text);
    draw->AddText({p.x + 6, y}, color, row.label.c_str());
    draw->AddText({p.x + 6 + width - ImGui::CalcTextSize(row.value.c_str()).x, y}, color, row.value.c_str());
  }
  ImGui::PopFont();
}
