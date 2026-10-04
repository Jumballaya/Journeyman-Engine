#pragma once

#include <string_view>
#include <unordered_map>

#include <glm/glm.hpp>

#include "ShaderHandle.hpp"
#include "TextureHandle.hpp"
#include "gl/Shader.hpp"
#include "gl/Texture2D.hpp"

// Owns every GPU texture and shader program and hands out handles to them.
// Main thread only (needs the GL context).
class GpuResources {
 public:
  // RGBA8 pixels, rows top to bottom.
  TextureHandle createTexture(int width, int height, const void* rgba, bool linear = false);
  // Transparent texture filled later with subUpload (atlases, glyph pages).
  TextureHandle createEmptyTexture(int width, int height, std::string_view filter);
  bool subUploadTexture(TextureHandle texture, int x, int y, int w, int h, const void* rgba);
  // Takes ownership of an existing GL texture (render-target copies).
  TextureHandle adopt(gl::Texture2D&& texture);
  void release(TextureHandle texture);

  gl::Texture2D* texture(TextureHandle handle);
  glm::vec2 textureSize(TextureHandle handle) const;

  // Full vertex + fragment program; throws on compile/link errors.
  ShaderHandle createShader(const std::string& vertex, const std::string& fragment);
  // Effect/transition shader; sources without #version get the prelude (shaders.hpp).
  // Invalid handle (logged) on compile errors.
  ShaderHandle createPostShader(std::string_view fragment, std::string_view debugName);
  gl::Shader* shader(ShaderHandle handle);

  void clear();

 private:
  std::unordered_map<TextureHandle, gl::Texture2D> _textures;
  std::unordered_map<ShaderHandle, gl::Shader> _shaders;
  uint32_t _nextTextureId = 1;
  uint32_t _nextShaderId = 1;
};
