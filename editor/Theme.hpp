#pragma once

#include <imgui.h>

#include <string>

// The editor's look: one neutral ramp, one accent, semantic colors, fonts and
// sizes. Everything drawn by hand takes its colors from here.
namespace theme {

// The colors in use: the dark or the light palette (setAppearance), so read
// them each frame rather than keeping copies.
// Elevation, ground first: app ground, panels, fields and headers, hover and popups.
inline ImVec4 bg0, bg1, bg2, bg3, bg4, border;
inline ImVec4 text, textDim, textFaint;
// What's selected (rows, list items): a neutral lift, so orange stays for the
// active tool, primary actions and focus.
inline ImVec4 selection;
// The accent marks focus, the active tool and primary actions; accentBright is
// the most prominent of the three on the current ground.
inline ImVec4 accent, accentBright, accentDeep;
inline ImVec4 success, warning, error, info, violet;
// Axis colors for vector fields and gizmos.
inline ImVec4 axisX, axisY, axisZ;

enum class Appearance { Dark, Light, System };
// Switches every color, ImGui's included. System follows the OS (macOS and
// Windows; elsewhere it's dark) and keeps following it through refresh().
void setAppearance(Appearance appearance);
Appearance appearance();
bool isLight();  // the light palette is in use
// Once a frame: picks up an OS appearance change while on System.
void refresh();
const char* appearanceName(Appearance appearance);  // "dark", "light", "system"
Appearance appearanceNamed(const std::string& name);  // System for anything else

inline ImU32 u32(ImVec4 c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32({c.x, c.y, c.z, c.w * alpha}); }
inline ImVec4 withAlpha(ImVec4 c, float alpha) { return {c.x, c.y, c.z, alpha}; }

// Type sizes (pixels before DPI scale).
inline constexpr float sizeSmall = 12.0f;
inline constexpr float sizeBody = 14.0f;
inline constexpr float sizeTitle = 16.0f;
inline constexpr float sizeDisplay = 26.0f;

// One radius scale: controls 5, overlays 8.
inline constexpr float radius = 5.0f;
inline constexpr float radiusOverlay = 8.0f;

struct Fonts {
  ImFont* medium = nullptr;  // Geist + Phosphor icons; regular is ImGui's default font
  ImFont* semibold = nullptr;
  ImFont* mono = nullptr;  // Geist Mono + icons
  ImFont* iconFill = nullptr;  // Phosphor Fill alone (solid play/pause, dots)
};
const Fonts& fonts();

// Loads the fonts and sets every ImGui style value and color. Once, after ImGui::CreateContext.
void apply();

}  // namespace theme
