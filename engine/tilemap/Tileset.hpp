#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "../renderer2d/TextureHandle.hpp"

// Edge masks: a bit is set where the neighbour on that side is a different
// terrain (see TileDef::joins).
namespace edges {
constexpr uint8_t N = 1, E = 2, S = 4, W = 8;
}

struct TileImage {
  TextureHandle texture;
  glm::vec4 texRect{0.0f, 0.0f, 1.0f, 1.0f};
  glm::vec2 size{0.0f};  // pixels
};

// What one map character is: how it looks, whether it blocks, its tags.
struct TileDef {
  bool solid = false;
  std::vector<std::string> tags;
  // Characters counted as the same terrain when choosing edge images;
  // empty = only this character.
  std::string joins;
  // Drawn beneath: the first of these characters found among the four
  // neighbours, else the last (e.g. "," then "." = road beside a road, else grass).
  std::string under;
  bool anchorBottomLeft = false;  // big images grow right and up from their cell
  float frameDuration = 0.25f;
  // images[mask][frame]; one mask (0) unless the image depends on edges.
  std::vector<std::vector<TileImage>> images;

  bool joinsWith(char self, char other) const {
    return joins.empty() ? other == self : joins.find(other) != std::string::npos;
  }
  bool hasTag(std::string_view tag) const {
    for (const auto& t : tags)
      if (t == tag) return true;
    return false;
  }
  // The image for this edge mask at `time` seconds; null if it has none.
  const TileImage* image(uint8_t mask, float time) const;
};

// Map characters to tile definitions, from JSON:
//   {"atlas": "assets/atlases/sprites.atlas.json", "tiles": {"#": {
//      "image": "brick" | "path_{mask}" | "water_{mask}_{frame}" | "{theme}ground",
//      "frames": 3, "frameDuration": 0.35, "solid": true, "tags": ["deadly"],
//      "joins": ",<>", "under": ",.", "anchor": "bottom-left",
//      "edges": [{"open": "N", "closed": "S", "image": "ground_top"}]}}}
// Image names without '#' or '/' are regions of "atlas"; {name} is replaced
// from `vars`. Characters with no definition are empty and open.
class Tileset {
 public:
  using Resolve = std::function<std::optional<TileImage>(const std::string& reference)>;

  // Unresolvable images are reported through `onMissing` and left blank.
  static Tileset parse(const nlohmann::json& json, const nlohmann::json& vars, const Resolve& resolve,
                       const std::function<void(const std::string&)>& onMissing = {});

  const TileDef* find(char c) const {
    auto it = _tiles.find(c);
    return it == _tiles.end() ? nullptr : &it->second;
  }

 private:
  std::unordered_map<char, TileDef> _tiles;
};
