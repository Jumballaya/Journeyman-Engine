#include "Thumbnails.hpp"

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
  Texture& t = _textures[file.string()];
  if (t.modified == modified) return t.id ? &t : nullptr;
  t.modified = modified;  // a broken image is decoded once per change, not every frame

  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load(file.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);
  if (!pixels) return t.id ? &t : nullptr;
  t.width = w;
  t.height = h;
  if (!t.id) glGenTextures(1, &t.id);  // a reload keeps the GL name
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
  return &t;
}

Thumbnails::Picture Thumbnails::Texture::whole() const {
  return {static_cast<ImTextureID>(id), {0, 0}, {1, 1}, ImVec2(static_cast<float>(width), static_cast<float>(height))};
}

const Thumbnails::Atlas* Thumbnails::atlas(const Project& project, const std::string& path) {
  const fs::path built = project.buildDir() / path;
  std::error_code ec;
  const auto modified = fs::last_write_time(built, ec);
  if (ec) return nullptr;
  Atlas& atlas = _atlases[path];
  if (atlas.modified != modified) {
    atlas = {{}, {}, modified};  // an unreadable atlas is parsed once per change
    std::ifstream in(built);
    const auto json = nlohmann::json::parse(in, nullptr, false);
    if (json.contains("image") && json.contains("regions")) {
      atlas.image = json["image"].get<std::string>();
      for (const auto& [name, rect] : json["regions"].items()) atlas.regions[name] = rect.get<std::array<int, 4>>();
    }
  }
  return atlas.image.empty() ? nullptr : &atlas;
}

std::optional<Thumbnails::Picture> Thumbnails::get(const Project& project, const std::string& reference) {
  const size_t hash = reference.find('#');
  if (hash == std::string::npos) {
    const Texture* t = texture(project.abs(reference));
    return t ? std::optional(t->whole()) : std::nullopt;
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

std::optional<Thumbnails::Picture> Thumbnails::get(const Project& project, const std::string& reference,
                                                    const std::array<int, 4>& rect) {
  auto whole = get(project, reference);
  if (!whole || whole->size.x <= 0 || whole->size.y <= 0) return whole;
  const auto [x, y, w, h] = rect;
  const ImVec2 span{whole->uv1.x - whole->uv0.x, whole->uv1.y - whole->uv0.y};
  auto uv = [&](int px, int py) {
    return ImVec2{whole->uv0.x + span.x * px / whole->size.x, whole->uv0.y + span.y * py / whole->size.y};
  };
  return Picture{whole->texture, uv(x, y), uv(x + w, y + h), ImVec2(static_cast<float>(w), static_cast<float>(h))};
}

std::optional<Thumbnails::Packed> Thumbnails::packed(const Project& project, const std::string& path) {
  const Atlas* a = atlas(project, path);
  if (!a) return std::nullopt;
  const Texture* t = texture(project.buildDir() / a->image);
  if (!t) return std::nullopt;
  return Packed{t->whole(), a->regions};
}

std::vector<std::string> Thumbnails::regions(const Project& project, const std::string& path) {
  std::vector<std::string> out;
  if (const Atlas* a = atlas(project, path)) {
    for (const auto& [name, _] : a->regions) out.push_back(name);
  }
  return out;
}
