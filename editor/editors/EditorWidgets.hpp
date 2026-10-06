#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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

// The region picker alone, as a popup opened with ImGui::OpenPopup(id).
// True (with `value` set) when a region is picked.
bool regionPopup(const char* id, const Project& project, const std::string& atlas, std::string& value);

// A searchable grid of the project's images, as a popup opened with
// ImGui::OpenPopup(id). True (with `value` set) when one is clicked; with
// `stayOpen` it stays up for more. `marked` images show a check.
bool imagePopup(const char* id, const Project& project, std::string& value, bool stayOpen = false,
                const std::function<bool(const std::string&)>& marked = {});

// Tiled custom properties ([{name, type, value}] in `holder`) as rows, each
// edited by its type and removable, and a new one added by name (typed into
// `draft`) and type. `edit` applies a change to the holder as one undoable
// step; properties named in `skip` are shown elsewhere.
using PropertyEdit = std::function<void(const std::string& label, const std::function<void(nlohmann::ordered_json&)>& change,
                                        const std::string& mergeKey)>;
void properties(const nlohmann::ordered_json& holder, std::string& draft, const PropertyEdit& edit,
                const std::vector<std::string>& skip = {});

// Tile `id` of a Tiled tileset (`json`, at project path `path`), or nothing without an image.
std::optional<Thumbnails::Picture> tilePicture(const Project& project, const nlohmann::ordered_json& json,
                                               const std::string& path, uint32_t id);

}  // namespace widgets
