#pragma once

#include <string>

#include <imgui.h>

#include "Thumbnails.hpp"

class Project;

// Widgets shared by the asset editors.
namespace widgets {

// A checkerboard behind transparent pixels, filling [a, b].
void checker(ImDrawList* draw, ImVec2 a, ImVec2 b, float cell = 8.0f);

// Draws `picture` as large as fits in [a, b], centered; whole-pixel scaling
// when enlarging, so pixel art stays crisp.
void fitted(ImDrawList* draw, const Thumbnails::Picture& picture, ImVec2 a, ImVec2 b, float alpha = 1.0f);

// A text field for an atlas region name, with a button opening a searchable
// grid of the atlas's regions. True when `value` changed (typed or picked).
bool regionField(const char* id, const Project& project, const std::string& atlas, std::string& value,
                 const char* hint = "region");

// The region picker alone, as a popup opened with ImGui::OpenPopup(id).
// True (with `value` set) when a region is picked.
bool regionPopup(const char* id, const Project& project, const std::string& atlas, std::string& value);

}  // namespace widgets
