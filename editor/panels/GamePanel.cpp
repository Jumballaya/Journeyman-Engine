// The Game view: the running game, scaled to fit (or to whole pixels), with
// keyboard focus following the view.

#include <algorithm>
#include <cmath>

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
  if (overGame || std::any_of(std::begin(_buttonsDown), std::end(_buttonsDown), [](bool b) { return b; })) {
    game->mouseMove((io.MousePos.x - at.x) * fb, (io.MousePos.y - at.y) * fb);
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
  ImGui::PopStyleVar(2);
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
  if (editor.paused()) {
    const char* text = ICON_PAUSE "  Paused";
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 p{at.x + (size.x - ts.x) * 0.5f - 12, at.y + 14};
    draw->AddRectFilled(p, {p.x + ts.x + 24, p.y + ts.y + 12}, theme::u32(theme::bg0, 0.85f), theme::radiusOverlay);
    draw->AddText({p.x + 12, p.y + 6}, theme::u32(theme::accent), text);
  }
}
