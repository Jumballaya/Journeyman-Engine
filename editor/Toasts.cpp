#include "Toasts.hpp"

#include <algorithm>

#include <imgui.h>

#include "Icons.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

constexpr double kLifetime = 4.0;
constexpr double kActionLifetime = 12.0;  // time to read it and decide
constexpr double kFade = 0.25;

ImVec4 colorOf(Toasts::Kind kind) {
  switch (kind) {
    case Toasts::Kind::Success: return theme::success;
    case Toasts::Kind::Warning: return theme::warning;
    case Toasts::Kind::Error: return theme::error;
    case Toasts::Kind::Info: break;
  }
  return theme::info;
}

const char* iconOf(Toasts::Kind kind) {
  switch (kind) {
    case Toasts::Kind::Success: return ICON_CHECK_CIRCLE;
    case Toasts::Kind::Warning: return ICON_WARNING;
    case Toasts::Kind::Error: return ICON_WARNING_OCTAGON;
    case Toasts::Kind::Info: break;
  }
  return ICON_INFO;
}

}  // namespace

void Toasts::show(Kind kind, std::string title, std::string body, std::string action, std::function<void()> onAction) {
  // The same notice twice in a row refreshes instead of stacking.
  for (Toast& t : _toasts) {
    if (!t.dismissed && t.title == title && t.body == body) {
      t.born = ImGui::GetTime();
      return;
    }
  }
  _toasts.push_back({kind, std::move(title), std::move(body), std::move(action), std::move(onAction), ImGui::GetTime()});
  if (_toasts.size() > 5) _toasts.erase(_toasts.begin());
}

void Toasts::draw() {
  const double now = ImGui::GetTime();
  auto lifetime = [](const Toast& t) { return t.action.empty() ? kLifetime : kActionLifetime; };
  std::erase_if(_toasts, [&](const Toast& t) {
    return t.dismissed || (t.kind != Kind::Error && now - t.born > lifetime(t) + kFade);
  });
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float width = 320.0f;
  float bottom = vp->WorkPos.y + vp->WorkSize.y - 36.0f;  // above the status bar

  for (size_t i = _toasts.size(); i-- > 0;) {
    Toast& t = _toasts[i];
    const double age = now - t.born;
    const bool fades = t.kind != Kind::Error;
    float alpha = static_cast<float>(std::min(1.0, age / kFade));
    if (fades && age > lifetime(t)) alpha = static_cast<float>(std::max(0.0, 1.0 - (age - lifetime(t)) / kFade));
    const float slide = (1.0f - std::min(1.0f, static_cast<float>(age / kFade))) * 16.0f;

    const float h = t.height > 0 ? t.height : 64.0f;
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x - width - 16.0f + slide, bottom - h});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme::radiusOverlay);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 12});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg3);
    const std::string id = "##toast" + std::to_string(reinterpret_cast<uintptr_t>(&t)) + std::to_string(t.born);
    ImGui::Begin(id.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);
    const ImVec4 color = colorOf(t.kind);
    const ImVec2 pos = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled({pos.x, pos.y + 8}, {pos.x + 3, pos.y + ImGui::GetWindowHeight() - 8},
                                              theme::u32(color, alpha), 2.0f);
    ImGui::TextColored(color, "%s", iconOf(t.kind));
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
        t.onAction();
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
}

void Toasts::dismiss(const std::string& title) {
  for (Toast& t : _toasts) {
    if (t.title == title) t.dismissed = true;
  }
}
