#pragma once

#include <imgui.h>

// The editor's look: one neutral ramp, one accent, semantic colors, fonts and
// sizes. Everything drawn by hand takes its colors from here.
namespace theme {

// Elevation, darkest first: app ground, panels, fields and headers, hover and popups.
inline constexpr ImVec4 bg0{0.051f, 0.055f, 0.063f, 1.0f};  // #0D0E10
inline constexpr ImVec4 bg1{0.094f, 0.098f, 0.110f, 1.0f};  // #18191C
inline constexpr ImVec4 bg2{0.125f, 0.133f, 0.149f, 1.0f};  // #202226
inline constexpr ImVec4 bg3{0.165f, 0.176f, 0.196f, 1.0f};  // #2A2D32
inline constexpr ImVec4 bg4{0.212f, 0.224f, 0.247f, 1.0f};  // #36393F
inline constexpr ImVec4 border{0.180f, 0.192f, 0.212f, 1.0f};  // #2E3136

inline constexpr ImVec4 text{0.894f, 0.902f, 0.918f, 1.0f};       // #E4E6EA
inline constexpr ImVec4 textDim{0.608f, 0.631f, 0.667f, 1.0f};    // #9BA1AA
inline constexpr ImVec4 textFaint{0.361f, 0.384f, 0.420f, 1.0f};  // #5C626B

// What's selected (rows, list items): a neutral lift, so orange stays for the
// active tool, primary actions and focus.
inline constexpr ImVec4 selection{0.894f, 0.902f, 0.918f, 0.10f};

// The accent marks focus, the active tool and primary actions.
inline constexpr ImVec4 accent{0.941f, 0.533f, 0.243f, 1.0f};       // #F0883E
inline constexpr ImVec4 accentBright{1.0f, 0.639f, 0.388f, 1.0f};   // #FFA363
inline constexpr ImVec4 accentDeep{0.784f, 0.412f, 0.157f, 1.0f};   // #C86928

inline constexpr ImVec4 success{0.341f, 0.671f, 0.353f, 1.0f};  // #57AB5A
inline constexpr ImVec4 warning{0.910f, 0.773f, 0.278f, 1.0f};  // #E8C547
inline constexpr ImVec4 error{0.898f, 0.325f, 0.294f, 1.0f};    // #E5534B
inline constexpr ImVec4 info{0.424f, 0.714f, 1.0f, 1.0f};       // #6CB6FF

// Axis colors for vector fields and gizmos.
inline constexpr ImVec4 axisX{0.906f, 0.369f, 0.341f, 1.0f};
inline constexpr ImVec4 axisY{0.447f, 0.769f, 0.380f, 1.0f};
inline constexpr ImVec4 axisZ{0.373f, 0.600f, 0.937f, 1.0f};

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
