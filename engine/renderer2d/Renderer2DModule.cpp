#include "Renderer2DModule.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <random>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/logger/logging.hpp"
#include "../core/app/WindowEvents.hpp"
#include "Renderer2DSystem.hpp"
#include "SpriteAnimationComponent.hpp"
#include "SpriteAnimationSystem.hpp"
#include "SpriteComponent.hpp"
#include "Traits.hpp"
#include "posteffects/BuiltinEffects.hpp"

// Loading GL needs the window's context.
template <>
struct ModuleTraits<Renderer2DModule> {
  using Provides = TypeList<Renderer2DTag>;
  using DependsOn = TypeList<OpenGLContextTag>;
};

REGISTER_MODULE(Renderer2DModule)

namespace {

std::string canonical(std::string_view path) {
  return std::filesystem::path(path).lexically_normal().generic_string();
}

std::optional<glm::vec4> readColor(const nlohmann::json& json, const char* key) {
  if (!json.contains(key) || !json[key].is_array() || json[key].size() != 4) return std::nullopt;
  const auto c = json[key].get<std::array<float, 4>>();
  return glm::vec4(c[0], c[1], c[2], c[3]);
}

// config.renderer: { logicalWidth, logicalHeight, clearColor, letterboxColor }
RenderSettings readSettings(const nlohmann::json& config) {
  RenderSettings settings;
  if (!config.contains("renderer")) return settings;
  const auto& r = config["renderer"];
  settings.logicalWidth = r.value("logicalWidth", 0);
  settings.logicalHeight = r.value("logicalHeight", 0);
  settings.clearColor = readColor(r, "clearColor").value_or(settings.clearColor);
  settings.letterboxColor = readColor(r, "letterboxColor").value_or(settings.letterboxColor);
  return settings;
}

}  // namespace

void Renderer2DModule::initialize(Engine& app) {
  _app = &app;
  int width = 1280, height = 720;
  // Render targets match the framebuffer, which is larger than the window on HiDPI.
  if (app.embedded() && app.viewSize().width > 0) {
    width = app.viewSize().width;
    height = app.viewSize().height;
  } else if (auto* context = glfwGetCurrentContext()) {
    glfwGetFramebufferSize(context, &width, &height);
  }
  if (!_renderer.initialize(width, height, readSettings(app.getManifest().config))) {
    throw std::runtime_error("Renderer2D: OpenGL failed to load");
  }
  _renderer.setPresentsToScreen(!app.embedded());

  registerAssetTypes(app);
  registerComponents(app);
  app.getWorld().registerSystem<SpriteAnimationSystem>();
  app.getWorld().registerSystem<Renderer2DSystem>(_renderer);

  app.getEventBus().subscribe<events::WindowResized>(
      EVT_WindowResize, [this](const events::WindowResized& e) { _renderer.resize(e.width, e.height); });

  // Effects, camera offset and shake belong to the scene that set them.
  app.getSceneManager().addUnloadListener([this]() {
    _renderer.chain().clear();
    _cameraBase = glm::vec2(0.0f);
    _shakeRemaining = 0.0f;
  });
  app.getSceneManager().addTransitionListener({
      .onBegin = [this](const TransitionConfig& config) {
        const ShaderHandle shader = shaderFor(config.shader);
        if (!config.shader.empty() && !shader.isValid()) {
          JM_LOG_WARN("[Renderer2D] transition shader '{}' isn't loaded; crossfading", config.shader);
        }
        _renderer.beginTransition(shader);
      },
      .onProgress = [this](float progress) { _renderer.setTransitionProgress(progress); },
      .onEnd = [this]() { _renderer.endTransition(); },
  });

  bindScriptApi(app);
  JM_LOG_INFO("[Renderer2D] initialized");
}

void Renderer2DModule::registerAssetTypes(Engine& app) {
  AssetManager& assets = app.getAssetManager();

  auto decodeImage = [this](const RawAsset& asset, const AssetHandle& handle) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(asset.data.data(), static_cast<int>(asset.data.size()), &w, &h,
                                            &channels, STBI_rgb_alpha);
    if (!pixels) {
      JM_LOG_ERROR("[Renderer2D] image '{}' failed to decode: {}", asset.filePath.string(), stbi_failure_reason());
      return;
    }
    _images.insert(handle, _renderer.resources().createTexture(w, h, pixels));
    stbi_image_free(pixels);
  };
  assets.addAssetConverter({".png", ".jpg", ".jpeg"}, decodeImage);
  assets.addAssetTypeConverter("image", decodeImage);

  // Built atlas: {"image": "...atlas.png", "width", "height", "regions": {name: [x, y, w, h]}}
  auto decodeAtlas = [this, &assets](const RawAsset& asset, const AssetHandle& handle) {
    const auto json = nlohmann::json::parse(asset.data.begin(), asset.data.end(), nullptr, false);
    if (json.is_discarded() || !json.contains("image") || !json.contains("regions")) {
      JM_LOG_ERROR("[Renderer2D] atlas '{}' is not a built atlas (run jm build)", asset.filePath.string());
      return;
    }
    const TextureHandle* texture = _images.get(assets.loadAsset(json["image"].get<std::string>()));
    if (!texture) return;
    std::unordered_map<std::string, std::array<int, 4>> regions;
    for (auto& [name, rect] : json["regions"].items()) regions[name] = rect.get<std::array<int, 4>>();
    _atlases.loadAtlas(handle, asset.filePath, *texture, json.value("width", 0u), json.value("height", 0u), regions);
  };
  assets.addAssetConverter({".atlas.json"}, decodeAtlas);
  assets.addAssetTypeConverter("atlas", decodeAtlas);

  auto compileShader = [this](const RawAsset& asset, const AssetHandle&) {
    const std::string path = canonical(asset.filePath.generic_string());
    const ShaderHandle shader = _renderer.resources().createPostShader(
        std::string_view(reinterpret_cast<const char*>(asset.data.data()), asset.data.size()), path);
    if (shader.isValid()) _shaders[path] = shader;
  };
  assets.addAssetConverter({".frag"}, compileShader);
  assets.addAssetTypeConverter("shader", compileShader);

  for (const BuiltinEffect& builtin : builtinEffects()) {
    _shaders["builtin:" + std::string(builtin.name)] =
        _renderer.resources().createPostShader(builtin.body, builtin.name);
  }
}

void Renderer2DModule::registerComponents(Engine& app) {
  app.getWorld().registerComponent<SpriteComponent>({
      .fromJson = [this](SpriteComponent& c, const nlohmann::json& json, EntityId) {
        if (json.contains("texture")) {
          const std::string reference = json["texture"].get<std::string>();
          if (auto image = resolveImage(reference)) {
            c.texture = image->texture;
            c.texRect = image->texRect;
          } else {
            JM_LOG_ERROR("[Renderer2D] sprite texture '{}' not found", reference);
          }
        }
        c.color = readColor(json, "color").value_or(c.color);
        c.texRect = readColor(json, "texRect").value_or(c.texRect);
        if (json.contains("shadow")) {
          const auto& shadow = json["shadow"];
          c.shadow.offset.x = shadow.value("x", c.shadow.offset.x);
          c.shadow.offset.y = shadow.value("y", c.shadow.offset.y);
          c.shadow.scale = shadow.value("scale", c.shadow.scale);
          c.shadow.layer = shadow.value("layer", c.shadow.layer);
          c.shadow.color = readColor(shadow, "color").value_or(glm::vec4(0.0f, 0.0f, 0.0f, 0.3f));
        }
      },
      .scriptFields = {
          scriptField<SpriteComponent>("r", [](SpriteComponent& c) -> float& { return c.color.r; }),
          scriptField<SpriteComponent>("g", [](SpriteComponent& c) -> float& { return c.color.g; }),
          scriptField<SpriteComponent>("b", [](SpriteComponent& c) -> float& { return c.color.b; }),
          scriptField<SpriteComponent>("a", [](SpriteComponent& c) -> float& { return c.color.a; }),
          scriptField<SpriteComponent>("shadowX", [](SpriteComponent& c) -> float& { return c.shadow.offset.x; }),
          scriptField<SpriteComponent>("shadowY", [](SpriteComponent& c) -> float& { return c.shadow.offset.y; }),
          scriptField<SpriteComponent>("shadowScale", [](SpriteComponent& c) -> float& { return c.shadow.scale; }),
          scriptField<SpriteComponent>("shadowLayer", [](SpriteComponent& c) -> float& { return c.shadow.layer; }),
          scriptField<SpriteComponent>("shadowR", [](SpriteComponent& c) -> float& { return c.shadow.color.r; }),
          scriptField<SpriteComponent>("shadowG", [](SpriteComponent& c) -> float& { return c.shadow.color.g; }),
          scriptField<SpriteComponent>("shadowB", [](SpriteComponent& c) -> float& { return c.shadow.color.b; }),
          scriptField<SpriteComponent>("shadowAlpha", [](SpriteComponent& c) -> float& { return c.shadow.color.a; }),
      },
      .schema = {"Sprite", "Rendering", "Draws an image or atlas region at the transform",
                 {FieldSchema::asset("texture", {".png", ".jpg", ".jpeg", ".atlas.json#"}, "Image, or atlas#region"),
                  FieldSchema::color("color", {1, 1, 1, 1}, "Tint; alpha fades the sprite"),
                  FieldSchema::group("shadow",
                                     {FieldSchema::number("x", 0, "Offset right"),
                                      FieldSchema::number("y", 0, "Offset up"),
                                      FieldSchema::number("scale", 1, "Size relative to the sprite", 0, 4, 0.01f),
                                      FieldSchema::number("layer", 0, "z of the shadow"),
                                      FieldSchema::color("color", {0, 0, 0, 0.3}, "")},
                                     "A drop shadow drawn beneath")}},
  });

  // {"atlasPath": "...atlas.json", "current": "idle",
  //  "animations": {"idle": {"regions": ["a", "b"], "frameDuration": 0.1, "loop": true}}}
  app.getWorld().registerComponent<SpriteAnimationComponent>({
      .fromJson = [this](SpriteAnimationComponent& c, const nlohmann::json& json, EntityId) {
        const std::string atlas = json.value("atlasPath", std::string());
        for (const auto& [name, spec] : json.value("animations", nlohmann::json::object()).items()) {
          SpriteAnimationComponent::Animation animation;
          animation.frameDuration = std::max(0.001f, spec.value("frameDuration", animation.frameDuration));
          animation.loop = spec.value("loop", animation.loop);
          for (const auto& region : spec.value("regions", nlohmann::json::array())) {
            const std::string reference = atlas + "#" + region.get<std::string>();
            if (auto image = resolveImage(reference)) {
              animation.frames.push_back({image->texture, image->texRect});
            } else {
              JM_LOG_ERROR("[Renderer2D] animation '{}' frame '{}' not found", name, reference);
            }
          }
          c.animations[name] = std::move(animation);
        }
        c.play(json.value("current", std::string()));
      },
      .schema = {"Sprite Animation", "Rendering", "Flipbook animations from an atlas",
                 {FieldSchema::asset("atlasPath", {".atlas.json"}, "Atlas the frames come from"),
                  FieldSchema::text("current", "", "Animation playing at start"),
                  FieldSchema::json("animations",
                                    "{\"name\": {\"regions\": [...], \"frameDuration\": 0.1, \"loop\": true}}")}},
  });
}

std::optional<Renderer2DModule::Image> Renderer2DModule::resolveImage(const std::string& reference) {
  try {
    Image image;
    if (const size_t hash = reference.find('#'); hash != std::string::npos) {
      const std::string atlas = reference.substr(0, hash);
      _app->getAssetManager().loadAsset(atlas);
      auto region = _atlases.lookupByPath(atlas, reference.substr(hash + 1));
      if (!region) return std::nullopt;
      image.texture = region->first;
      image.texRect = region->second;
    } else {
      const TextureHandle* texture = _images.get(_app->getAssetManager().loadAsset(reference));
      if (!texture) return std::nullopt;
      image.texture = *texture;
    }
    image.size = _renderer.resources().textureSize(image.texture) * glm::vec2(image.texRect.z, image.texRect.w);
    return image;
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[Renderer2D] image '{}' failed to load: {}", reference, e.what());
    return std::nullopt;
  }
}

ShaderHandle Renderer2DModule::shaderFor(std::string_view nameOrPath) const {
  auto it = _shaders.find(canonical(_app->getManifest().resolve(nameOrPath, ".frag")));
  return it == _shaders.end() ? ShaderHandle{} : it->second;
}

void Renderer2DModule::bindScriptApi(Engine& app) {
  ScriptManager& s = app.getScriptManager();
  PostEffectChain& chain = _renderer.chain();

  // Effects: handles are u32 ids; 0 = failed. Shaders compile at asset-load
  // time on the main thread, so these are pure lookups.
  s.bind("__jmEffectAddBuiltin", [this, &chain](std::string name) -> uint32_t {
    for (const BuiltinEffect& builtin : builtinEffects()) {
      if (builtin.name != name) continue;
      PostEffect effect;
      effect.shader = shaderFor("builtin:" + name);
      for (const auto& [uniform, value] : builtin.defaults) effect.uniforms[uniform] = value;
      return chain.add(std::move(effect)).id;
    }
    JM_LOG_ERROR("[Renderer2D] unknown builtin effect '{}'", name);
    return 0;
  });
  s.bind("__jmEffectAddCustom", [this, &chain](std::string path) -> uint32_t {
    const ShaderHandle shader = shaderFor(path);
    if (!shader.isValid()) {
      JM_LOG_ERROR("[Renderer2D] effect shader '{}' isn't loaded (list it in .jm.json assets)", path);
      return 0;
    }
    PostEffect effect;
    effect.shader = shader;
    return chain.add(std::move(effect)).id;
  });
  s.bind("__jmEffectRemove", [&chain](uint32_t id) { chain.remove(PostEffectHandle{id}); });
  s.bind("__jmEffectSetEnabled", [&chain](uint32_t id, bool on) { chain.setEnabled(PostEffectHandle{id}, on); });
  // `count` = number of components used (1 float … 4 vec4).
  s.bind("__jmEffectSetUniform", [&chain](uint32_t id, std::string name, int32_t count, float x, float y, float z,
                                          float w) {
    UniformValue value = x;
    if (count == 2) value = glm::vec2(x, y);
    if (count == 3) value = glm::vec3(x, y, z);
    if (count == 4) value = glm::vec4(x, y, z, w);
    chain.setUniform(PostEffectHandle{id}, name, value);
  });

  s.bind("__jmCameraShake", [this](float amplitude, float seconds) {
    const float current = _shakeRemaining > 0.0f ? _shakeAmplitude * (_shakeRemaining / _shakeDuration) : 0.0f;
    if (seconds > 0.0f && amplitude >= current) {  // strongest shake wins
      _shakeAmplitude = amplitude;
      _shakeDuration = _shakeRemaining = seconds;
    }
  });
  s.bind("__jmCameraSetPosition", [this](float x, float y) { _cameraBase = {x, y}; });
  // Writes the view's center (without shake) and half size, in world units.
  s.bind("__jmCameraView", [this](host::WasmBytes out) {
    if (out.size < sizeof(float) * 4) return;
    const glm::vec2 half = glm::vec2(_renderer.logicalSize()) * 0.5f / _renderer.camera().zoom();
    const float view[4] = {_cameraBase.x, _cameraBase.y, half.x, half.y};
    std::memcpy(out.data, view, sizeof(view));
  });
  s.bind("__jmRendererSetClearColor", [this](float r, float g, float b, float a) {
    _pendingClearColor = glm::vec4(r, g, b, a);
  });

  s.bind("__jmSpritePlay", [&app](EntityId entity, std::string animation, bool restart) {
    auto start = [&app, entity, animation, restart]() {
      auto* anim = app.getWorld().getComponent<SpriteAnimationComponent>(entity);
      return anim && (restart ? anim->restart(animation) : anim->play(animation));
    };
    return start() || app.getSpawner().whenSpawned(entity, start);  // spawned this frame: plays once it exists
  });
  s.bind("__jmSpriteAnimation", [&app](EntityId entity) -> std::optional<std::string> {
    auto* anim = app.getWorld().getComponent<SpriteAnimationComponent>(entity);
    if (!anim) return std::nullopt;
    return anim->current;
  });
  // Images load on the main thread, so the change shows from the next frame.
  s.bind("__jmSpriteSetTexture", [this](EntityId entity, std::string reference) {
    std::lock_guard lock(_textureMutex);
    _pendingTextures.emplace_back(entity, std::move(reference));
  });
  s.bind("__jmSpriteFinished", [&app](EntityId entity) {
    auto* anim = app.getWorld().getComponent<SpriteAnimationComponent>(entity);
    return anim && anim->finished;
  });
}

void Renderer2DModule::applyPendingTextures(World& world) {
  std::vector<std::pair<EntityId, std::string>> pending;
  {
    std::lock_guard lock(_textureMutex);
    pending.swap(_pendingTextures);
  }
  for (const auto& [entity, reference] : pending) {
    auto* sprite = world.getComponent<SpriteComponent>(entity);
    if (!sprite) continue;
    auto image = resolveImage(reference);
    if (!image) {
      JM_LOG_ERROR("[Renderer2D] sprite texture '{}' not found", reference);
      continue;
    }
    sprite->texture = image->texture;
    sprite->texRect = image->texRect;
    if (auto* anim = world.getComponent<SpriteAnimationComponent>(entity)) anim->current.clear();  // stop animating over it
  }
}

void Renderer2DModule::tickMainThread(Engine& app, float dt) {
  applyPendingTextures(app.getWorld());
  if (_pendingClearColor) {
    _renderer.setClearColor(*_pendingClearColor);
    _pendingClearColor.reset();
  }

  glm::vec2 shake(0.0f);
  if (_shakeRemaining > 0.0f) {
    static std::mt19937 rng{1942u};
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    shake = glm::vec2(unit(rng), unit(rng)) * _shakeAmplitude * (_shakeRemaining / _shakeDuration);
    _shakeRemaining -= dt;
  }
  if (_editorView) {
    _renderer.camera().setPosition(_editorView->center);
    _renderer.camera().setZoom(_editorView->zoom);
  } else {
    _renderer.camera().setPosition(_cameraBase + shake);
  }
  for (auto& pass : _overlayPasses) pass(_renderer);
  _renderer.endFrame();
  captureIfRequested(app);
  ++_frame;
}

void Renderer2DModule::setEditorView(std::optional<EditorView> view) {
  _editorView = view;
  _renderer.setLogicalSizeOverride(view ? std::optional(view->logicalSize) : std::nullopt);
  if (!view) _renderer.camera().setZoom(1.0f);
}

void Renderer2DModule::captureIfRequested(const Engine& app) {
  const DevOptions& dev = app.getDevOptions();
  if (dev.captureDir.empty() ||
      std::find(dev.captureFrames.begin(), dev.captureFrames.end(), _frame) == dev.captureFrames.end()) {
    return;
  }
  int w = 0, h = 0;
  const std::vector<uint8_t> pixels = _renderer.readFinalFrame(w, h);
  std::filesystem::create_directories(dev.captureDir);
  char name[32];
  std::snprintf(name, sizeof(name), "frame_%05llu.png", static_cast<unsigned long long>(_frame));
  const std::string path = (dev.captureDir / name).string();
  if (stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4)) {
    JM_LOG_INFO("[Renderer2D] captured {}", path);
  } else {
    JM_LOG_ERROR("[Renderer2D] couldn't write {}", path);
  }
}

void Renderer2DModule::shutdown(Engine&) {
  _renderer.shutdown();
  JM_LOG_INFO("[Renderer2D] shutdown");
}
