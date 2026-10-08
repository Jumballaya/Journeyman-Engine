#include "Theme.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

#define EMBEDDED(name)                \
  extern const uint8_t name##_data[]; \
  extern const size_t name##_size;
EMBEDDED(geist_regular)
EMBEDDED(geist_medium)
EMBEDDED(geist_semibold)
EMBEDDED(geist_mono)
EMBEDDED(phosphor)
EMBEDDED(phosphor_fill)
#undef EMBEDDED

namespace theme {

#ifdef __APPLE__
bool systemPrefersLight();  // SystemAppearance.mm
#else
bool systemPrefersLight() {
#ifdef _WIN32
  DWORD light = 0, size = sizeof(light);
  return RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      "AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS &&
         light != 0;
#else
  return false;  // no portable way to ask on Linux: dark, unless chosen
#endif
}
#endif

namespace {

Fonts gFonts;

struct Palette {
  ImVec4 bg0, bg1, bg2, bg3, bg4, border, text, textDim, textFaint, selection;
  ImVec4 accent, accentBright, accentDeep, success, warning, error, info, violet, axisX, axisY, axisZ;
  ImVec4 dim;  // what modals darken the rest with
};

ImVec4 rgb(uint32_t hex, float alpha = 1.0f) {
  return {((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha};
}

const Palette kDark{
    rgb(0x0D0E10), rgb(0x18191C), rgb(0x202226), rgb(0x2A2D32), rgb(0x36393F), rgb(0x2E3136),
    rgb(0xE4E6EA), rgb(0x9BA1AA), rgb(0x5C626B), rgb(0xE4E6EA, 0.10f),
    rgb(0xF0883E), rgb(0xFFA363), rgb(0xC86928), rgb(0x57AB5A), rgb(0xE8C547), rgb(0xE5534B), rgb(0x6CB6FF),
    rgb(0xC78CF2), rgb(0xE75E57), rgb(0x72C461), rgb(0x5F99EF),
    rgb(0x0D0E10, 0.65f),
};

// The same ramp turned over: a light gray ground, near-white panels, white
// popups; accents deepened so they hold up on white (accentBright is the
// darkest orange here, as it's the one that must stand out).
const Palette kLight{
    rgb(0xE4E6EA), rgb(0xF4F5F7), rgb(0xFFFFFF), rgb(0xE7E9EC), rgb(0xD9DCE1), rgb(0xD2D6DB),
    rgb(0x1C1F24), rgb(0x585F6B), rgb(0x8E959F), rgb(0x1C1F24, 0.08f),
    rgb(0xE07020), rgb(0xB85410), rgb(0xA04A0E), rgb(0x2D8A3E), rgb(0x9A7000), rgb(0xCC3A33), rgb(0x1F6FCC),
    rgb(0x8A4FD0), rgb(0xD23F38), rgb(0x3D9A2E), rgb(0x2F6FD6),
    rgb(0x1C1F24, 0.30f),
};

Appearance gAppearance = Appearance::Dark;
bool gLight = false;
ImVec4 gDim;

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

ImFont* addFont(const uint8_t* data, size_t size, bool merge = false, ImVec2 offset = {}, float minAdvance = 0.0f) {
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;  // the bytes live in the executable
  config.MergeMode = merge;
  config.GlyphOffset = offset;
  config.GlyphMinAdvanceX = minAdvance;
  config.OversampleH = 2;
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(data), static_cast<int>(size), sizeBody,
                                                    &config);
}

// A text face with the icon font merged in, so labels can mix text and icons.
ImFont* addTextFont(const uint8_t* data, size_t size) {
  ImFont* font = addFont(data, size);
  addFont(phosphor_data, phosphor_size, true, {0.0f, 2.0f}, sizeBody);
  return font;
}

void setColors(ImGuiStyle& style) {
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = textFaint;
  c[ImGuiCol_WindowBg] = bg1;
  c[ImGuiCol_ChildBg] = withAlpha(bg1, 0.0f);
  c[ImGuiCol_PopupBg] = bg2;
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = withAlpha(bg0, 0.0f);
  // Fields are a light wash, so they read as inset on panels and popups alike.
  c[ImGuiCol_FrameBg] = withAlpha(text, 0.055f);
  c[ImGuiCol_FrameBgHovered] = withAlpha(text, 0.085f);
  c[ImGuiCol_FrameBgActive] = withAlpha(text, 0.10f);
  c[ImGuiCol_TitleBg] = bg0;
  c[ImGuiCol_TitleBgActive] = bg0;
  c[ImGuiCol_TitleBgCollapsed] = bg0;
  c[ImGuiCol_MenuBarBg] = bg0;
  c[ImGuiCol_ScrollbarBg] = withAlpha(bg1, 0.0f);
  c[ImGuiCol_ScrollbarGrab] = bg3;
  c[ImGuiCol_ScrollbarGrabHovered] = bg4;
  c[ImGuiCol_ScrollbarGrabActive] = textFaint;
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accentBright;
  c[ImGuiCol_Button] = bg3;
  c[ImGuiCol_ButtonHovered] = bg4;
  c[ImGuiCol_ButtonActive] = withAlpha(text, 0.22f);
  c[ImGuiCol_Header] = selection;
  c[ImGuiCol_HeaderHovered] = withAlpha(text, 0.05f);
  c[ImGuiCol_HeaderActive] = withAlpha(text, 0.14f);
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.6f);
  c[ImGuiCol_SeparatorActive] = accent;
  c[ImGuiCol_ResizeGrip] = withAlpha(bg0, 0.0f);
  c[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.6f);
  c[ImGuiCol_ResizeGripActive] = accent;
  c[ImGuiCol_InputTextCursor] = accentBright;
  c[ImGuiCol_Tab] = bg0;
  c[ImGuiCol_TabHovered] = mix(bg0, bg1, 0.35f);  // short of the selected tab, which is the panel's color
  c[ImGuiCol_TabSelected] = bg1;
  c[ImGuiCol_TabSelectedOverline] = withAlpha(accent, 0.0f);  // the selected tab is the panel's color: no stripe
  c[ImGuiCol_TabDimmed] = bg0;
  c[ImGuiCol_TabDimmedSelected] = bg1;
  c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(accent, 0.0f);
  c[ImGuiCol_DockingPreview] = withAlpha(accent, 0.35f);
  c[ImGuiCol_DockingEmptyBg] = bg0;
  c[ImGuiCol_PlotLines] = textDim;
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_TableHeaderBg] = bg2;
  c[ImGuiCol_TableBorderStrong] = border;
  c[ImGuiCol_TableBorderLight] = withAlpha(border, 0.6f);
  c[ImGuiCol_TableRowBg] = withAlpha(bg1, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = withAlpha(text, 0.02f);
  c[ImGuiCol_TextLink] = accentBright;
  c[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.30f);
  c[ImGuiCol_TreeLines] = border;
  c[ImGuiCol_DragDropTarget] = accent;
  c[ImGuiCol_DragDropTargetBg] = withAlpha(accent, 0.08f);
  c[ImGuiCol_NavCursor] = accent;
  c[ImGuiCol_NavWindowingHighlight] = withAlpha(text, 0.7f);
  c[ImGuiCol_NavWindowingDimBg] = gDim;
  c[ImGuiCol_ModalWindowDimBg] = gDim;
}

void use(const Palette& p, bool light) {
  bg0 = p.bg0, bg1 = p.bg1, bg2 = p.bg2, bg3 = p.bg3, bg4 = p.bg4, border = p.border;
  text = p.text, textDim = p.textDim, textFaint = p.textFaint, selection = p.selection;
  accent = p.accent, accentBright = p.accentBright, accentDeep = p.accentDeep;
  success = p.success, warning = p.warning, error = p.error, info = p.info, violet = p.violet;
  axisX = p.axisX, axisY = p.axisY, axisZ = p.axisZ;
  gDim = p.dim;
  gLight = light;
  if (ImGui::GetCurrentContext()) setColors(ImGui::GetStyle());
}

bool wantsLight(Appearance appearance) {
  return appearance == Appearance::Light || (appearance == Appearance::System && systemPrefersLight());
}

}  // namespace

const Fonts& fonts() { return gFonts; }

void setAppearance(Appearance appearance) {
  gAppearance = appearance;
  const bool light = wantsLight(appearance);
  use(light ? kLight : kDark, light);
}

Appearance appearance() { return gAppearance; }

bool isLight() { return gLight; }

void refresh() {
  if (gAppearance != Appearance::System) return;
  // Asking the OS costs a little: once a second is soon enough.
  static auto last = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::seconds(1)) return;
  last = now;
  if (const bool light = systemPrefersLight(); light != gLight) use(light ? kLight : kDark, light);
}

const char* appearanceName(Appearance appearance) {
  switch (appearance) {
    case Appearance::Dark: return "dark";
    case Appearance::Light: return "light";
    default: return "system";
  }
}

Appearance appearanceNamed(const std::string& name) {
  if (name == "dark") return Appearance::Dark;
  if (name == "light") return Appearance::Light;
  return Appearance::System;
}

void apply() {
  addTextFont(geist_regular_data, geist_regular_size);  // the first font is the default
  gFonts.medium = addTextFont(geist_medium_data, geist_medium_size);
  gFonts.semibold = addTextFont(geist_semibold_data, geist_semibold_size);
  gFonts.mono = addTextFont(geist_mono_data, geist_mono_size);
  gFonts.iconFill = addFont(phosphor_fill_data, phosphor_fill_size, false, {0.0f, 2.0f}, sizeBody);

  ImGuiStyle& style = ImGui::GetStyle();
  style.FontSizeBase = sizeBody;
  style.WindowPadding = {10, 10};
  style.FramePadding = {8, 5};
  style.CellPadding = {6, 4};
  style.ItemSpacing = {8, 6};
  style.ItemInnerSpacing = {6, 4};
  style.IndentSpacing = 14;
  style.ScrollbarSize = 10;
  style.GrabMinSize = 10;
  style.WindowBorderSize = 0;
  style.ChildBorderSize = 0;
  style.TabBarBorderSize = 0;
  style.TabBarOverlineSize = 0;
  style.WindowRounding = 0;
  style.ChildRounding = radius;
  style.FrameRounding = radius;
  style.PopupRounding = radiusOverlay;
  style.ScrollbarRounding = radiusOverlay;
  style.GrabRounding = radius;
  style.TabRounding = radius;
  style.WindowTitleAlign = {0.0f, 0.5f};
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.SelectableTextAlign = {0.0f, 0.5f};
  style.SeparatorTextBorderSize = 1;
  style.SeparatorTextPadding = {0, 4};
  style.DockingSeparatorSize = 2;  // the ground shows through as a gutter between panels
  style.DisplaySafeAreaPadding = {0, 0};
  setAppearance(gAppearance);  // the colors (dark until the editor reads the user's choice)
}

}  // namespace theme
