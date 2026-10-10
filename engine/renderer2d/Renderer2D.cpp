#include "Renderer2D.hpp"

#include "Letterbox.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>

#include <glm/gtc/matrix_transform.hpp>

#include "../core/logger/logging.hpp"
#include "common.hpp"
#include "posteffects/BuiltinEffects.hpp"
#include "shaders.hpp"

bool Renderer2D::initialize(int framebufferWidth, int framebufferHeight, const RenderSettings& settings, bool gpu) {
  _gpu = gpu;
  _resources.setGpu(gpu);
  _settings = settings;
  if (!gpu) {
    _spriteShader = _resources.createShader(sprite_vertex_shader, sprite_fragment_shader);
    _crossfade = _resources.createPostShader(kCrossfadeTransition, "crossfade");
    _white = _resources.createTexture(1, 1, nullptr);
    resize(framebufferWidth, framebufferHeight);
    return true;
  }
  if (!gladLoadGL(glfwGetProcAddress)) {
    JM_LOG_ERROR("[Renderer2D] OpenGL failed to load");
    return false;
  }
  _spriteShader = _resources.createShader(sprite_vertex_shader, sprite_fragment_shader);
  _crossfade = _resources.createPostShader(kCrossfadeTransition, "crossfade");
  const uint8_t white[4] = {255, 255, 255, 255};
  _white = _resources.createTexture(1, 1, white);

  _batch.initialize();
  _shadows.initialize();
  static constexpr std::array<float, 20> quad = {-1, -1, 0, 0, 0, 1, -1, 0, 1, 0, -1, 1, 0, 0, 1, 1, 1, 0, 1, 1};
  _quad.initialize(quad);
  resize(framebufferWidth, framebufferHeight);
  return true;
}

void Renderer2D::shutdown() {
  _resources.clear();
  if (!_gpu) return;
  _batch.destroy();
  _shadows.destroy();
  for (gl::FrameBuffer& frame : _swap) frame.destroy();
  _quad.destroy();
}

void Renderer2D::resize(int w, int h) {
  if (w <= 0 || h <= 0) return;  // minimized
  _width = w;
  _height = h;
  _logicalW = _logicalOverride ? _logicalOverride->x : _settings.logicalWidth > 0 ? _settings.logicalWidth : w;
  _logicalH = _logicalOverride ? _logicalOverride->y : _settings.logicalHeight > 0 ? _settings.logicalHeight : h;

  _viewport = letterbox::fit(w, h, _logicalW, _logicalH);

  if (_gpu) {
    for (gl::FrameBuffer& frame : _swap) frame.resize(w, h);
  }
  _camera.setViewport(_logicalW, _logicalH);
}

void Renderer2D::setLogicalSizeOverride(std::optional<glm::ivec2> size) {
  if ((size && (size->x <= 0 || size->y <= 0)) || size == _logicalOverride) return;
  _logicalOverride = size;
  resize(_width, _height);
}

void Renderer2D::drawSprite(const glm::mat4& transform, const glm::vec4& color, const glm::vec4& texRect,
                            TextureHandle texture, float z, Lit lit) {
  _worldItems.push_back(
      {SpriteInstance{transform, color, texRect}, texture.isValid() ? texture : _white, z, lit, normalMap(texture)});
}

TextureHandle Renderer2D::normalMap(TextureHandle texture) const {
  auto it = _normals.find(texture);
  return it == _normals.end() ? TextureHandle{} : it->second;
}

void Renderer2D::drawLine(glm::vec2 a, glm::vec2 b, const glm::vec4& color, float width, float z, Lit lit) {
  const glm::vec2 d = b - a;
  const float length = glm::length(d);
  if (length == 0.0f || width <= 0.0f) return;
  glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3((a + b) * 0.5f, 0.0f));  // z sorts; it's not depth
  m = glm::rotate(m, std::atan2(d.y, d.x), glm::vec3(0.0f, 0.0f, 1.0f));
  m = glm::scale(m, glm::vec3(length * 0.5f, width * 0.5f, 1.0f));  // a quad spans ±scale
  drawSprite(m, color, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), TextureHandle{}, z, lit);
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
  std::stable_sort(_worldItems.begin(), _worldItems.end(), [](const DrawItem& a, const DrawItem& b) {
    return a.z != b.z ? a.z < b.z : a.texture.id < b.texture.id;
  });
  if (_gpu && _drawing) drawFrame();
  // Keep the frame as data (swapping, not copying); the next one starts empty.
  _drawnWorld.swap(_worldItems);
  _drawnScreen.swap(_screenItems);
  _worldItems.clear();
  _screenItems.clear();
}

void Renderer2D::drawFrame() {
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
}

TextureHandle Renderer2D::copyFinalFrame() {
  if (!_gpu) return {};
  const gl::FrameBuffer& src = _swap[_current];
  gl::Texture2D copy;
  copy.initialize(src.width(), src.height());  // binds it
  src.bind(GL_READ_FRAMEBUFFER);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, src.width(), src.height());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  return _resources.adopt(std::move(copy));
}

std::vector<uint8_t> Renderer2D::readFinalFrame(int& width, int& height) {
  if (!_gpu) {
    width = height = 0;
    return {};
  }
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
  // All of the pass's instances in one upload; consecutive same-texture items
  // then become one instanced draw of their range.
  _instances.clear();
  for (const DrawItem& item : items) _instances.push_back(item.instance);
  _batch.upload(_instances);
  auto bind = [this](TextureHandle texture, int slot) {
    gl::Texture2D* t = _resources.texture(texture);
    (t ? t : _resources.texture(_white))->bindToSlot(slot);
  };
  for (size_t i = 0; i < items.size();) {
    const size_t first = i;
    while (i < items.size() && items[i].batchesWith(items[first])) ++i;
    const DrawItem& run = items[first];
    if (_litShader) {
      _litShader->uniform("u_lit", run.lit == Lit::Yes ? 1 : 0);
      _litShader->uniform("u_hasNormal", run.normal.isValid() ? 1 : 0);
      bind(run.normal, 1);
    }
    bind(run.texture, 0);
    _batch.draw(first, i - first);
  }
}

void Renderer2D::renderScene() {
  gl::Shader& sprite = *_resources.shader(_spriteShader);
  applyLighting(sprite);  // before the scene's framebuffer: it may draw the shadow map
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

  sprite.bind();
  sprite.uniform("u_texture", 0);
  sprite.uniform("u_normal", 1);
  sprite.uniform("u_projView", _camera.projView());
  _litShader = _lighting.on ? &sprite : nullptr;
  drawItems(_worldItems);  // sorted back to front by endFrame
  _litShader = nullptr;

  sprite.uniform("u_lit", 0);  // the UI is drawn as made
  sprite.uniform("u_hasNormal", 0);
  sprite.uniform("u_projView", glm::ortho(0.0f, static_cast<float>(_logicalW), static_cast<float>(_logicalH), 0.0f,
                                         -1.0f, 1.0f));
  drawItems(_screenItems);

  sprite.unbind();
  glDisable(GL_SCISSOR_TEST);
}

std::vector<const Lighting::Light*> Renderer2D::shownLights() const {
  // The shader takes 32: the ones nearest the camera (the others are off screen, or barely matter).
  std::vector<const Lighting::Light*> nearest;
  for (const auto& light : _lighting.lights) nearest.push_back(&light);
  const glm::vec2 center = _camera.position();
  const size_t count = std::min<size_t>(nearest.size(), kShadowRows);
  std::partial_sort(nearest.begin(), nearest.begin() + count, nearest.end(), [&](auto* a, auto* b) {
    return glm::distance(a->position, center) - a->radius < glm::distance(b->position, center) - b->radius;
  });
  nearest.resize(count);
  return nearest;
}

void Renderer2D::applyLighting(gl::Shader& sprite) {
  const std::vector<const Lighting::Light*> lights = _lighting.on ? shownLights() : std::vector<const Lighting::Light*>{};
  const std::vector<ShadowCaster> casters = shadowCasters(lights, _lighting.occluders);
  if (!casters.empty()) _shadows.build(casters);
  sprite.bind();
  sprite.uniform("u_lit", _lighting.on ? 1 : 0);
  sprite.uniform("u_shadows", casters.empty() ? 0 : 1);
  if (!_lighting.on) return;
  std::vector<glm::vec4> place, color;
  std::vector<float> shadow;
  for (const Lighting::Light* light : lights) {
    place.emplace_back(light->position, std::max(light->radius, 0.001f), std::max(light->falloff, 0.0f));
    color.emplace_back(light->color, std::max(light->height, 0.001f));  // 0 over a pixel: no direction
    shadow.push_back(light->shadowSoftness ? std::max(*light->shadowSoftness, 0.0f) : -1.0f);
  }
  sprite.uniform("u_ambient", _lighting.ambient);
  sprite.uniform("u_lightCount", static_cast<int>(lights.size()));
  sprite.uniform("u_lightPlace", place);
  sprite.uniform("u_lightColor", color);
  if (casters.empty()) return;
  sprite.uniform("u_lightShadow", shadow);
  sprite.uniform("u_shadowMap", 2);
  _shadows.bindToSlot(2);
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
  shader.uniform("u_time", _time);
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
