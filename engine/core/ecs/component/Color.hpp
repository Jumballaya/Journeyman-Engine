#pragma once

#include <optional>
#include <string_view>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// A component's color as a scene writes it (FieldSchema::color): [r, g, b, a]
// or [r, g, b] in 0..1, or "#rgb", "#rgba", "#rrggbb", "#rrggbbaa".
// nullopt for anything else; every module reads its colors through this.
std::optional<glm::vec4> colorOf(const nlohmann::json& value);
// "#rgb" .. "#rrggbbaa" (case-insensitive); nullopt for anything else.
std::optional<glm::vec4> hexColor(std::string_view text);
