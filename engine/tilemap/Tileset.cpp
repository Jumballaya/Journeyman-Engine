#include "Tileset.hpp"

#include <cmath>

namespace {

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
  for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
    text.replace(at, from.size(), to);
  }
}

uint8_t sides(const std::string& letters) {
  uint8_t mask = 0;
  for (char c : letters) {
    if (c == 'N') mask |= edges::N;
    if (c == 'E') mask |= edges::E;
    if (c == 'S') mask |= edges::S;
    if (c == 'W') mask |= edges::W;
  }
  return mask;
}

// The image name for one edge mask: the first matching "edges" rule, else "image".
std::string nameFor(const nlohmann::json& spec, uint8_t mask) {
  for (const auto& rule : spec.value("edges", nlohmann::json::array())) {
    const uint8_t open = sides(rule.value("open", std::string()));
    const uint8_t closed = sides(rule.value("closed", std::string()));
    if ((mask & open) == open && (mask & closed) == 0) return rule.value("image", std::string());
  }
  return spec.value("image", std::string());
}

}  // namespace

const TileImage* TileDef::image(uint8_t mask, float time) const {
  if (images.empty()) return nullptr;
  const auto& frames = images[images.size() == 1 ? 0 : mask];
  if (frames.empty()) return nullptr;
  const auto frame = static_cast<size_t>(std::floor(time / frameDuration)) % frames.size();
  return frames[frame].texture.isValid() ? &frames[frame] : nullptr;
}

Tileset Tileset::parse(const nlohmann::json& json, const nlohmann::json& vars, const Resolve& resolve,
                       const std::function<void(const std::string&)>& onMissing) {
  Tileset set;
  const std::string atlas = json.value("atlas", std::string());
  auto reference = [&](std::string name) {
    for (const auto& [key, value] : vars.items()) {
      if (value.is_string()) replaceAll(name, "{" + key + "}", value.get<std::string>());
    }
    if (name.find('#') == std::string::npos && name.find('/') == std::string::npos && !atlas.empty()) {
      name = atlas + "#" + name;
    }
    return name;
  };

  for (const auto& [key, spec] : json.value("tiles", nlohmann::json::object()).items()) {
    if (key.size() != 1 || !spec.is_object()) continue;
    TileDef def;
    def.solid = spec.value("solid", false);
    def.tags = spec.value("tags", std::vector<std::string>{});
    def.joins = spec.value("joins", std::string());
    def.under = spec.value("under", std::string());
    def.anchorBottomLeft = spec.value("anchor", std::string()) == "bottom-left";
    def.frameDuration = std::max(0.01f, spec.value("frameDuration", def.frameDuration));
    const int frames = std::max(1, spec.value("frames", 1));

    const std::string image = spec.value("image", std::string());
    const bool byMask = image.find("{mask}") != std::string::npos || spec.contains("edges");
    if (!image.empty() || spec.contains("edges")) {
      def.images.resize(byMask ? 16 : 1);
      for (uint8_t mask = 0; mask < def.images.size(); ++mask) {
        for (int frame = 0; frame < frames; ++frame) {
          std::string name = nameFor(spec, mask);
          replaceAll(name, "{mask}", std::to_string(mask));
          replaceAll(name, "{frame}", std::to_string(frame));
          const std::string ref = reference(name);
          auto resolved = resolve(ref);
          if (!resolved && onMissing) onMissing(ref);
          def.images[mask].push_back(resolved.value_or(TileImage{}));
        }
      }
    }
    set._tiles[key[0]] = std::move(def);
  }
  return set;
}
