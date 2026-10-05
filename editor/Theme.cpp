#include "Theme.hpp"

#include <cstddef>
#include <cstdint>

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

namespace {

Fonts gFonts;

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
  c[ImGuiCol_FrameBg] = bg2;
  c[ImGuiCol_FrameBgHovered] = bg3;
  c[ImGuiCol_FrameBgActive] = bg3;
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
  c[ImGuiCol_ButtonActive] = withAlpha(accent, 0.35f);
  c[ImGuiCol_Header] = withAlpha(accent, 0.20f);
  c[ImGuiCol_HeaderHovered] = withAlpha(text, 0.06f);
  c[ImGuiCol_HeaderActive] = withAlpha(accent, 0.28f);
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.6f);
  c[ImGuiCol_SeparatorActive] = accent;
  c[ImGuiCol_ResizeGrip] = withAlpha(bg0, 0.0f);
  c[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.6f);
  c[ImGuiCol_ResizeGripActive] = accent;
  c[ImGuiCol_InputTextCursor] = accentBright;
  c[ImGuiCol_Tab] = bg0;
  c[ImGuiCol_TabHovered] = bg2;
  c[ImGuiCol_TabSelected] = bg1;
  c[ImGuiCol_TabSelectedOverline] = accent;
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
  c[ImGuiCol_NavWindowingDimBg] = withAlpha(bg0, 0.6f);
  c[ImGuiCol_ModalWindowDimBg] = withAlpha(bg0, 0.65f);
}

}  // namespace

const Fonts& fonts() { return gFonts; }

void apply() {
  gFonts.regular = addTextFont(geist_regular_data, geist_regular_size);
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
  style.TouchExtraPadding = {0, 0};
  style.IndentSpacing = 14;
  style.ScrollbarSize = 10;
  style.GrabMinSize = 10;
  style.WindowBorderSize = 0;
  style.ChildBorderSize = 0;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.TabBorderSize = 0;
  style.TabBarBorderSize = 0;
  style.TabBarOverlineSize = 2;
  style.WindowRounding = 0;
  style.ChildRounding = radius;
  style.FrameRounding = radius;
  style.PopupRounding = radiusOverlay;
  style.ScrollbarRounding = radiusOverlay;
  style.GrabRounding = radius;
  style.TabRounding = radius;
  style.WindowTitleAlign = {0.0f, 0.5f};
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.ButtonTextAlign = {0.5f, 0.5f};
  style.SelectableTextAlign = {0.0f, 0.5f};
  style.SeparatorTextBorderSize = 1;
  style.SeparatorTextPadding = {0, 4};
  style.DockingSeparatorSize = 2;  // the ground shows through as a gutter between panels
  style.DisplaySafeAreaPadding = {0, 0};
  style.AntiAliasedLines = true;
  style.AntiAliasedFill = true;
  setColors(style);
}

}  // namespace theme
