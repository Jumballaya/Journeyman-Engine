#include "Toasts.hpp"

#include <algorithm>
#include <limits>

#include <imgui.h>

#include "Icons.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

constexpr double kLifetime = 4.0;
constexpr double kActionLifetime = 12.0;  // time to read it and decide
constexpr double kFade = 0.25;

struct Look {
  ImVec4 color;
  const char* icon;
};

Look lookOf(Toasts::Kind kind) {
  switch (kind) {
    case Toasts::Kind::Success: return {theme::success, ICON_CHECK_CIRCLE};
    case Toasts::Kind::Warning: return {theme::warning, ICON_WARNING};
    case Toasts::Kind::Error: return {theme::error, ICON_WARNING_OCTAGON};
    case Toasts::Kind::Info: break;
  }
  return {theme::info, ICON_INFO};
}

}  // namespace

void Toasts::show(Kind kind, std::string title, std::string body, std::string action, std::function<void()> onAction) {
  // A notice already showing refreshes instead of stacking.
  for (Toast& t : _toasts) {
    if (!t.dismissed && t.title == title && t.body == body) {
      t.born = ImGui::GetTime();
      return;
    }
  }
  _toasts.push_back({kind, std::move(title), std::move(body), std::move(action), std::move(onAction), ImGui::GetTime(), _nextId++});
  if (_toasts.size() > 5) _toasts.erase(_toasts.begin());
}

void Toasts::draw() {
  const double now = ImGui::GetTime();
  // When it starts fading: errors never do; one with an action waits for a decision.
  auto fadeAt = [](const Toast& t) {
    return t.kind == Kind::Error ? std::numeric_limits<double>::infinity() : t.action.empty() ? kLifetime : kActionLifetime;
  };
  std::erase_if(_toasts, [&](const Toast& t) { return t.dismissed || now - t.born > fadeAt(t) + kFade; });
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float width = 320.0f;
  float bottom = vp->WorkPos.y + vp->WorkSize.y - 36.0f;  // above the status bar
  std::function<void()> clicked;  // run after the loop: it may show toasts of its own

  for (size_t i = _toasts.size(); i-- > 0;) {
    Toast& t = _toasts[i];
    const double age = now - t.born;
    const double in = std::min(1.0, age / kFade);
    const float alpha = static_cast<float>(std::min(in, std::max(0.0, 1.0 - (age - fadeAt(t)) / kFade)));
    const float slide = static_cast<float>(1.0 - in) * 16.0f;

    const float h = t.height > 0 ? t.height : 64.0f;
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x - width - 16.0f + slide, bottom - h});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme::radiusOverlay);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 12});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg3);
    const std::string id = "##toast" + std::to_string(t.id);
    ImGui::Begin(id.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);
    const Look look = lookOf(t.kind);
    const ImVec2 pos = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled({pos.x, pos.y + 8}, {pos.x + 3, pos.y + ImGui::GetWindowHeight() - 8},
                                              theme::u32(look.color, alpha), 2.0f);
    ImGui::TextColored(look.color, "%s", look.icon);
    ImGui::SameLine(0, 10);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(width - 56.0f);
    ui::heading(t.title.c_str());
    if (!t.body.empty()) ui::dimText(t.body.c_str());
    ImGui::PopTextWrapPos();
    if (!t.action.empty()) {
      ImGui::Dummy({0, 2});
      ImGui::PushStyleColor(ImGuiCol_Text, theme::accentBright);
      if (ImGui::SmallButton(t.action.c_str()) && t.onAction) {
        clicked = t.onAction;
        t.dismissed = true;
      }
      ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos({pos.x + width - 30.0f, pos.y + 8.0f});
    if (ui::iconButton("close", ICON_X, nullptr, false, 0, 20.0f)) t.dismissed = true;
    t.height = ImGui::GetWindowHeight();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    bottom -= h + 8.0f;
  }
  if (clicked) clicked();
}

void Toasts::dismiss(const std::string& title) {
  for (Toast& t : _toasts) {
    if (t.title == title) t.dismissed = true;
  }
}
