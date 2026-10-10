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
  if (!_gpu) {
    const TextureHandle handle{_nextTextureId++};
    _sizes[handle] = {width, height};
    return handle;
  }
  gl::Texture2D texture;
  texture.initialize(width, height);
  texture.subUpload(0, 0, width, height, rgba);
  if (linear) texture.setFilter(GL_LINEAR);
  return adopt(std::move(texture));
}

TextureHandle GpuResources::createEmptyTexture(int width, int height, std::string_view filter) {
  if (!_gpu) return createTexture(width, height, nullptr);
  const std::vector<uint8_t> zeros(static_cast<size_t>(width) * height * 4, 0);
  return createTexture(width, height, zeros.data(), filter == "linear");
}

bool GpuResources::subUploadTexture(TextureHandle handle, int x, int y, int w, int h, const void* rgba) {
  if (!_gpu) return _sizes.contains(handle);
  gl::Texture2D* t = texture(handle);
  if (!t) {
    JM_LOG_ERROR("[GpuResources] subUpload: unknown texture");
    return false;
  }
  t->subUpload(x, y, w, h, rgba);
  return true;
}

void GpuResources::release(TextureHandle handle) {
  _textures.erase(handle);
  _sizes.erase(handle);
}
void GpuResources::release(ShaderHandle handle) { _shaders.erase(handle); }

gl::Texture2D* GpuResources::texture(TextureHandle handle) {
  auto it = _textures.find(handle);
  return it == _textures.end() ? nullptr : &it->second;
}

glm::vec2 GpuResources::textureSize(TextureHandle handle) const {
  if (auto size = _sizes.find(handle); size != _sizes.end()) return glm::vec2(size->second);
  auto it = _textures.find(handle);
  if (it == _textures.end()) return glm::vec2(0.0f);
  return glm::vec2(static_cast<float>(it->second.width()), static_cast<float>(it->second.height()));
}

ShaderHandle GpuResources::createShader(const std::string& vertex, const std::string& fragment) {
  if (!_gpu) return ShaderHandle{_nextShaderId++};  // nothing to compile: no errors either
  gl::Shader program;
  program.load(vertex, fragment);
  const ShaderHandle handle{_nextShaderId++};
  _shaders.emplace(handle, std::move(program));
  return handle;
}

std::string GpuResources::postSource(std::string_view fragment) {
  return fragment.find("#version") == std::string_view::npos ? std::string(post_effect_prelude) + std::string(fragment)
                                                             : std::string(fragment);
}

ShaderHandle GpuResources::createPostShader(std::string_view fragment, std::string_view debugName, std::string* error) {
  try {
    return createShader(screen_vertex_shader, postSource(fragment));
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[GpuResources] shader '{}' failed to compile:\n{}", debugName, e.what());
    if (error) *error = e.what();
    return {};
  }
}

bool GpuResources::replaceTexture(TextureHandle handle, int width, int height, const void* rgba) {
  if (!_gpu) {
    if (!_sizes.contains(handle)) return false;
    _sizes[handle] = {width, height};
    return true;
  }
  gl::Texture2D* t = texture(handle);
  if (!t) return false;
  if (t->width() != width || t->height() != height) t->resize(width, height);
  t->subUpload(0, 0, width, height, rgba);
  return true;
}

bool GpuResources::replacePostShader(ShaderHandle handle, std::string_view fragment, std::string_view debugName) {
  if (!_gpu) return true;
  if (!_shaders.contains(handle)) return false;
  try {
    gl::Shader program;
    program.load(screen_vertex_shader, postSource(fragment));
    _shaders.erase(handle);
    _shaders.emplace(handle, std::move(program));
    return true;
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[GpuResources] shader '{}' failed to compile (the old one stays):\n{}", debugName, e.what());
    return false;
  }
}

gl::Shader* GpuResources::shader(ShaderHandle handle) {
  auto it = _shaders.find(handle);
  return it == _shaders.end() ? nullptr : &it->second;
}

void GpuResources::clear() {
  _textures.clear();
  _sizes.clear();
  _shaders.clear();
}
