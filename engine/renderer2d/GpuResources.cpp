#include "GpuResources.hpp"

#include <string>
#include <vector>

#include "../core/logger/logging.hpp"
#include "shaders.hpp"

TextureHandle GpuResources::adopt(gl::Texture2D&& texture) {
  const TextureHandle handle{_nextTextureId++};
  _textures.emplace(handle, std::move(texture));
  return handle;
}

TextureHandle GpuResources::createTexture(int width, int height, const void* rgba, bool linear) {
  gl::Texture2D texture;
  texture.initialize(width, height);
  texture.subUpload(0, 0, width, height, rgba);
  if (linear) texture.setFilter(GL_LINEAR);
  return adopt(std::move(texture));
}

TextureHandle GpuResources::createEmptyTexture(int width, int height, std::string_view filter) {
  const std::vector<uint8_t> zeros(static_cast<size_t>(width) * height * 4, 0);
  return createTexture(width, height, zeros.data(), filter == "linear");
}

bool GpuResources::subUploadTexture(TextureHandle handle, int x, int y, int w, int h, const void* rgba) {
  gl::Texture2D* t = texture(handle);
  if (!t) {
    JM_LOG_ERROR("[GpuResources] subUpload: unknown texture");
    return false;
  }
  t->subUpload(x, y, w, h, rgba);
  return true;
}

void GpuResources::release(TextureHandle handle) { _textures.erase(handle); }
void GpuResources::release(ShaderHandle handle) { _shaders.erase(handle); }

gl::Texture2D* GpuResources::texture(TextureHandle handle) {
  auto it = _textures.find(handle);
  return it == _textures.end() ? nullptr : &it->second;
}

glm::vec2 GpuResources::textureSize(TextureHandle handle) const {
  auto it = _textures.find(handle);
  if (it == _textures.end()) return glm::vec2(0.0f);
  return glm::vec2(static_cast<float>(it->second.width()), static_cast<float>(it->second.height()));
}

ShaderHandle GpuResources::createShader(const std::string& vertex, const std::string& fragment) {
  gl::Shader program;
  program.load(vertex, fragment);
  const ShaderHandle handle{_nextShaderId++};
  _shaders.emplace(handle, std::move(program));
  return handle;
}

ShaderHandle GpuResources::createPostShader(std::string_view fragment, std::string_view debugName, std::string* error) {
  const std::string source = fragment.find("#version") == std::string_view::npos
                                 ? std::string(post_effect_prelude) + std::string(fragment)
                                 : std::string(fragment);
  try {
    return createShader(screen_vertex_shader, source);
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[GpuResources] shader '{}' failed to compile:\n{}", debugName, e.what());
    if (error) *error = e.what();
    return {};
  }
}

gl::Shader* GpuResources::shader(ShaderHandle handle) {
  auto it = _shaders.find(handle);
  return it == _shaders.end() ? nullptr : &it->second;
}

void GpuResources::clear() {
  _textures.clear();
  _shaders.clear();
}
