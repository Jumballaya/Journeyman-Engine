#include "Thumbnails.hpp"

#include <array>
#include <fstream>

#include <glad/gl.h>
#include <nlohmann/json.hpp>

#include "Project.hpp"
#include "stb_image.h"

namespace fs = std::filesystem;

Thumbnails& Thumbnails::instance() {
  static Thumbnails thumbnails;
  return thumbnails;
}

void Thumbnails::clear() {
  for (auto& [_, t] : _textures) glDeleteTextures(1, &t.id);
  _textures.clear();
  _atlases.clear();
}

const Thumbnails::Texture* Thumbnails::texture(const fs::path& file) {
  std::error_code ec;
  const auto modified = fs::last_write_time(file, ec);
  if (ec) return nullptr;
  auto it = _textures.find(file.string());
  if (it != _textures.end() && it->second.modified == modified) return &it->second;

  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load(file.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);
  if (!pixels) return nullptr;
  Texture t{0, w, h, modified};
  if (it != _textures.end()) t.id = it->second.id;  // reuse the GL name on reload
  if (!t.id) glGenTextures(1, &t.id);
  glBindTexture(GL_TEXTURE_2D, t.id);
  // Pixel art stays crisp when enlarged; downscaled tiles use linear to avoid shimmer.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(pixels);
  return &(_textures[file.string()] = t);
}

const Thumbnails::Atlas* Thumbnails::atlas(const Project& project, const std::string& path) {
  const fs::path built = project.buildDir() / path;
  std::error_code ec;
  const auto modified = fs::last_write_time(built, ec);
  if (ec) return nullptr;
  auto it = _atlases.find(path);
  if (it != _atlases.end() && it->second.modified == modified) return &it->second;
  std::ifstream in(built);
  const auto json = nlohmann::json::parse(in, nullptr, false);
  if (json.is_discarded() || !json.contains("image") || !json.contains("regions")) return nullptr;
  Atlas atlas{json["image"].get<std::string>(), {}, modified};
  for (const auto& [name, rect] : json["regions"].items()) atlas.regions[name] = rect.get<std::array<int, 4>>();
  return &(_atlases[path] = std::move(atlas));
}

std::optional<Thumbnails::Picture> Thumbnails::get(const Project& project, const std::string& reference) {
  const size_t hash = reference.find('#');
  if (hash == std::string::npos) {
    const Texture* t = texture(project.abs(reference));
    if (!t) return std::nullopt;
    return Picture{static_cast<ImTextureID>(t->id), {0, 0}, {1, 1}, ImVec2(static_cast<float>(t->width), static_cast<float>(t->height))};
  }
  const Atlas* a = atlas(project, reference.substr(0, hash));
  if (!a) return std::nullopt;
  auto region = a->regions.find(reference.substr(hash + 1));
  if (region == a->regions.end()) return std::nullopt;
  const Texture* t = texture(project.buildDir() / a->image);
  if (!t) return std::nullopt;
  const auto [x, y, w, h] = region->second;
  const float tw = static_cast<float>(t->width), th = static_cast<float>(t->height);
  return Picture{static_cast<ImTextureID>(t->id), {x / tw, y / th}, {(x + w) / tw, (y + h) / th},
                 ImVec2(static_cast<float>(w), static_cast<float>(h))};
}

std::vector<std::string> Thumbnails::regions(const Project& project, const std::string& path) {
  std::vector<std::string> out;
  if (const Atlas* a = atlas(project, path)) {
    for (const auto& [name, _] : a->regions) out.push_back(name);
  }
  return out;
}
