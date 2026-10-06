#include "Renderer2D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>

#include <glm/gtc/matrix_transform.hpp>

#include "../core/logger/logging.hpp"
#include "common.hpp"
#include "posteffects/BuiltinEffects.hpp"
#include "shaders.hpp"

bool Renderer2D::initialize(int framebufferWidth, int framebufferHeight, const RenderSettings& settings) {
  if (!gladLoadGL(glfwGetProcAddress)) {
    JM_LOG_ERROR("[Renderer2D] OpenGL failed to load");
    return false;
  }
  _settings = settings;
  _spriteShader = _resources.createShader(sprite_vertex_shader, sprite_fragment_shader);
  _crossfade = _resources.createPostShader(kCrossfadeTransition, "crossfade");
  const uint8_t white[4] = {255, 255, 255, 255};
  _white = _resources.createTexture(1, 1, white);

  _batch.initialize();
  static constexpr std::array<float, 20> quad = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, -1, 1, 0, 0, 1, 1, 1, 0, 1, 1};
  _quad.initialize(quad);
  resize(framebufferWidth, framebufferHeight);
  _start = std::chrono::steady_clock::now();
  return true;
}

void Renderer2D::shutdown() {
  _resources.clear();
  _batch.destroy();
  for (gl::FrameBuffer& frame : _swap) frame.destroy();
  _quad.destroy();
}

void Renderer2D::resize(int w, int h) {
  if (w <= 0 || h <= 0) return;  // minimized
  _width = w;
  _height = h;
  _logicalW = _logicalOverride ? _logicalOverride->x : _settings.logicalWidth > 0 ? _settings.logicalWidth : w;
  _logicalH = _logicalOverride ? _logicalOverride->y : _settings.logicalHeight > 0 ? _settings.logicalHeight : h;

  const float scale = std::min(static_cast<float>(w) / _logicalW, static_cast<float>(h) / _logicalH);
  const float vw = std::floor(_logicalW * scale), vh = std::floor(_logicalH * scale);
  _viewport = glm::vec4(std::floor((w - vw) * 0.5f), std::floor((h - vh) * 0.5f), vw, vh);

  for (gl::FrameBuffer& frame : _swap) frame.resize(w, h);
  _camera.setViewport(_logicalW, _logicalH);
}

void Renderer2D::setLogicalSizeOverride(std::optional<glm::ivec2> size) {
  if ((size && (size->x <= 0 || size->y <= 0)) || size == _logicalOverride) return;
  _logicalOverride = size;
  resize(_width, _height);
}

void Renderer2D::drawSprite(const glm::mat4& transform, const glm::vec4& color, const glm::vec4& texRect,
                            TextureHandle texture, float z) {
  std::lock_guard lock(_worldMutex);
  _worldItems.push_back({SpriteInstance{transform, color, texRect}, texture.isValid() ? texture : _white, z});
}

void Renderer2D::drawScreenQuad(const glm::vec4& logicalRect, const glm::vec4& color, const glm::vec4& texRect,
                                TextureHandle texture) {
  const float s = _screenTransform.scale;
  const glm::vec4 rect(logicalRect.x * s + _screenTransform.offset.x, logicalRect.y * s + _screenTransform.offset.y,
                       logicalRect.z * s, logicalRect.w * s);
  glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f, 0.0f));
  // Negative y: the screen projection is y-down, textures are authored y-up.
  m = glm::scale(m, glm::vec3(rect.z * 0.5f, -rect.w * 0.5f, 1.0f));
  _screenItems.push_back({SpriteInstance{m, color, texRect}, texture.isValid() ? texture : _white, 0.0f});
}

void Renderer2D::beginTransition(ShaderHandle shader) {
  if (_transition) _resources.release(_transition->oldFrame);
  _transition = Transition{copyFinalFrame(), shader.isValid() ? shader : _crossfade, 0.0f};
}

void Renderer2D::setTransitionProgress(float progress) {
  if (_transition) _transition->progress = progress;
}

void Renderer2D::endTransition() {
  if (!_transition) return;
  _resources.release(_transition->oldFrame);
  _transition.reset();
}

void Renderer2D::endFrame() {
  renderScene();
  _current = 0;
  glDisable(GL_BLEND);
  for (const PostEffect* effect : _chain.enabledEffects()) {
    if (gl::Shader* shader = _resources.shader(effect->shader)) fullscreenPass(*shader, effect->auxTexture, effect, 0.0f);
  }
  if (_transition) {
    if (gl::Shader* shader = _resources.shader(_transition->shader)) {
      fullscreenPass(*shader, _transition->oldFrame, nullptr, _transition->progress);
    }
  }
  present();
  _worldItems.clear();
  _screenItems.clear();
}

TextureHandle Renderer2D::copyFinalFrame() {
  const gl::FrameBuffer& src = _swap[_current];
  gl::Texture2D copy;
  copy.initialize(src.width(), src.height());  // binds it
  src.bind(GL_READ_FRAMEBUFFER);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, src.width(), src.height());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  return _resources.adopt(std::move(copy));
}

std::vector<uint8_t> Renderer2D::readFinalFrame(int& width, int& height) {
  const gl::FrameBuffer& src = _swap[_current];
  width = src.width();
  height = src.height();
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  src.bind(GL_READ_FRAMEBUFFER);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  const size_t row = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height / 2; ++y) {  // GL rows are bottom-up
    std::swap_ranges(pixels.begin() + y * row, pixels.begin() + (y + 1) * row, pixels.begin() + (height - 1 - y) * row);
  }
  return pixels;
}

void Renderer2D::drawItems(const std::vector<DrawItem>& items) {
  // Consecutive same-texture items become one instanced draw.
  for (size_t i = 0; i < items.size();) {
    const TextureHandle texture = items[i].texture;
    for (; i < items.size() && items[i].texture == texture; ++i) _batch.submit(items[i].instance);
    gl::Texture2D* t = _resources.texture(texture);
    (t ? t : _resources.texture(_white))->bindToSlot(0);
    _batch.draw();
  }
}

void Renderer2D::renderScene() {
  _swap[0].clear(_settings.letterboxColor);

  const auto vx = static_cast<GLint>(_viewport.x), vy = static_cast<GLint>(_viewport.y);
  const auto vw = static_cast<GLsizei>(_viewport.z), vh = static_cast<GLsizei>(_viewport.w);
  glEnable(GL_SCISSOR_TEST);
  glScissor(vx, vy, vw, vh);
  const auto& cc = _settings.clearColor;
  glClearColor(cc.r, cc.g, cc.b, cc.a);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(vx, vy, vw, vh);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  gl::Shader& sprite = *_resources.shader(_spriteShader);
  sprite.bind();
  sprite.uniform("u_texture", 0);

  sprite.uniform("u_projView", _camera.projView());
  std::stable_sort(_worldItems.begin(), _worldItems.end(), [](const DrawItem& a, const DrawItem& b) {
    return a.z != b.z ? a.z < b.z : a.texture.id < b.texture.id;
  });
  drawItems(_worldItems);

  sprite.uniform("u_projView", glm::ortho(0.0f, static_cast<float>(_logicalW), static_cast<float>(_logicalH), 0.0f,
                                         -1.0f, 1.0f));
  drawItems(_screenItems);

  sprite.unbind();
  glDisable(GL_SCISSOR_TEST);
}

void Renderer2D::fullscreenPass(gl::Shader& shader, TextureHandle aux, const PostEffect* effect, float progress) {
  gl::FrameBuffer& src = _swap[_current];
  gl::FrameBuffer& dst = _swap[_current ^ 1];
  dst.clear(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
  glViewport(0, 0, dst.width(), dst.height());

  shader.bind();
  src.color().bindToSlot(0);
  shader.uniform("u_primary", 0);
  if (gl::Texture2D* auxTexture = _resources.texture(aux)) {
    auxTexture->bindToSlot(1);
    shader.uniform("u_aux", 1);
  }
  shader.uniform("u_resolution", glm::vec2(static_cast<float>(dst.width()), static_cast<float>(dst.height())));
  shader.uniform("u_viewport", _viewport);
  shader.uniform("u_logical", glm::vec2(static_cast<float>(_logicalW), static_cast<float>(_logicalH)));
  shader.uniform("u_time", std::chrono::duration<float>(std::chrono::steady_clock::now() - _start).count());
  shader.uniform("u_progress", progress);
  if (effect) {
    for (const auto& [name, value] : effect->uniforms) {
      std::visit([&](const auto& v) { shader.uniform(name, v); }, value);
    }
  }
  _quad.bind();
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  shader.unbind();
  _current ^= 1;
}

void Renderer2D::present() {
  if (!_presentsToScreen) return;
  const gl::FrameBuffer& frame = _swap[_current];
  frame.bind(GL_READ_FRAMEBUFFER);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, frame.width(), frame.height(), 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}
