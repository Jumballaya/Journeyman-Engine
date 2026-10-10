#include "Renderer2DModule.hpp"

#include "Letterbox.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <future>
#include <random>
#include <sstream>

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
  _shakeRng.seed(static_cast<std::mt19937::result_type>(app.getSeeds().next()));
  int width = 1280, height = 720;
  // No GL at all (JM_RENDERER=none): frames are kept as data, at the window's size.
  const bool gpu = app.getDevOptions().renderer != "none";
  // Render targets match the framebuffer, which is larger than the window on HiDPI.
  if (app.embedded() && app.viewSize().width > 0) {
    width = app.viewSize().width;
    height = app.viewSize().height;
  } else if (!gpu) {
    const nlohmann::json& config = app.getManifest().config;
    const nlohmann::json win = config.contains("window") ? config["window"] : nlohmann::json::object();
    width = win.value("width", width);
    height = win.value("height", height);
  } else if (auto* context = glfwGetCurrentContext()) {
    // Headless renders at the window's size on every machine (macOS reports a
    // 2x backing size even unscaled), so captures compare across platforms.
    if (app.getDevOptions().headless) glfwGetWindowSize(context, &width, &height);
    else glfwGetFramebufferSize(context, &width, &height);
  }
  if (!_renderer.initialize(width, height, readSettings(app.getManifest().config), gpu)) {
    throw std::runtime_error("Renderer2D: OpenGL failed to load");
  }
  _renderer.setPresentsToScreen(!app.embedded());
  app.setFramebufferSize(width, height);

  registerAssetTypes(app);
  app.getWorld().registerSystem<SpriteAnimationSystem>();
  app.getWorld().registerSystem<Renderer2DSystem>(_renderer);

  app.getEventBus().subscribe<events::WindowResized>(
      EVT_WindowResize, [this](const events::WindowResized& e) { _renderer.resize(e.width, e.height); });
  app.getEventBus().subscribe<events::MouseMove>(EVT_MouseMove, [this](const events::MouseMove& e) {
    _pointer = {e.x, e.y};
    _pointerSeen = true;
  });

  // Effects, camera offset and shake belong to the scene that set them.
  app.getSceneManager().addUnloadListener([this]() {
    _renderer.chain().clear();
    _cameraBase = glm::vec2(0.0f);
    _cameraZoom = 1.0f;
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

  JM_LOG_INFO("[Renderer2D] initialized");
}

void Renderer2DModule::registerAssetTypes(Engine& app) {
  AssetManager& assets = app.getAssetManager();

  auto decodeImage = [this](const RawAsset& asset, const AssetHandle& handle) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(asset.data.data(), static_cast<int>(asset.data.size()), &w, &h,
                                            &channels, STBI_rgb_alpha);
    if (!pixels) {
      JM_REPORT_ERROR((ErrorSource{asset.filePath.generic_string()}), "[Renderer2D] image '{}' failed to decode: {}", asset.filePath.string(), stbi_failure_reason());
      return;
    }
    const TextureHandle texture = _renderer.resources().createTexture(w, h, pixels);
    _images.insert(handle, texture);
    _imagePaths[texture.id] = asset.filePath.generic_string();
    stbi_image_free(pixels);
  };
  assets.addAssetConverter({".png", ".jpg", ".jpeg"}, decodeImage);
  assets.addAssetTypeConverter("image", decodeImage);

  // Built atlas: {"image": "...atlas.png", "width", "height", "regions": {name: [x, y, w, h]}}
  auto decodeAtlas = [this, &assets](const RawAsset& asset, const AssetHandle& handle) {
    const auto json = nlohmann::json::parse(asset.data.begin(), asset.data.end(), nullptr, false);
    if (json.is_discarded() || !json.contains("image") || !json.contains("regions")) {
      JM_REPORT_ERROR((ErrorSource{asset.filePath.generic_string()}), "[Renderer2D] atlas '{}' is not a built atlas (run jm build)",
                    asset.filePath.string());
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
        if (const std::string texture = json.value("texture", std::string()); !texture.empty()) {
          setSpriteImage(c, texture);
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
          dumpedWith(scriptField<SpriteComponent>("shadowX", [](SpriteComponent& c) -> float& { return c.shadow.offset.x; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowY", [](SpriteComponent& c) -> float& { return c.shadow.offset.y; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowScale", [](SpriteComponent& c) -> float& { return c.shadow.scale; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowLayer", [](SpriteComponent& c) -> float& { return c.shadow.layer; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowR", [](SpriteComponent& c) -> float& { return c.shadow.color.r; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowG", [](SpriteComponent& c) -> float& { return c.shadow.color.g; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowB", [](SpriteComponent& c) -> float& { return c.shadow.color.b; }), "shadowAlpha"),
          dumpedWith(scriptField<SpriteComponent>("shadowAlpha", [](SpriteComponent& c) -> float& { return c.shadow.color.a; }), "shadowAlpha"),
      },
      .schema = {"Sprite", "Rendering", "Draws an image or atlas region at the transform",
                 {FieldSchema::asset("texture", {".png", ".jpg", ".jpeg", ".atlas.json#"}, "Image, or atlas#region; none: a solid quad in color"),
                  FieldSchema::color("color", {1, 1, 1, 1}, "Tint; alpha fades the sprite"),
                  FieldSchema::json("texRect", "[u, v, w, h], 0..1: the part of the image drawn (an atlas region sets it)"),
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
        const nlohmann::json animations = json.value("animations", nlohmann::json::object());  // named: items() of a temporary dangles
        for (const auto& [name, spec] : animations.items()) {
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
    JM_REPORT_ERROR((ErrorSource{reference}), "[Renderer2D] image '{}' failed to load: {}", reference, e.what());
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
      JM_REPORT_ERROR((ErrorSource{path}), "[Renderer2D] effect shader '{}' isn't loaded (list it in .jm.json assets)", path);
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
  s.bind("__jmCameraSetZoom", [this](float zoom) { _cameraZoom = std::isfinite(zoom) && zoom > 0.0f ? zoom : 1.0f; });
  // Writes the game view's center (without shake), half size in world units, and zoom.
  s.bind("__jmCameraView", [this](host::WasmBytes out) {
    if (out.size < sizeof(float) * 5) return;
    const glm::vec2 half = glm::vec2(_renderer.logicalSize()) * 0.5f / _cameraZoom;
    const float view[5] = {_cameraBase.x, _cameraBase.y, half.x, half.y, _cameraZoom};
    std::memcpy(out.data, view, sizeof(view));
  });
  // The pointer in screen (UI) pixels, y down from the game's top-left: x, y, and 1 when it's over the game.
  s.bind("__jmPointer", [this](host::WasmBytes out) {
    if (out.size < sizeof(float) * 3) return;
    const glm::vec2 size(_renderer.logicalSize());
    const glm::vec2 p = letterbox::toLogical(_pointer, _renderer.gameViewport(), _renderer.frameSize().y,
                                             static_cast<int>(size.x));
    const float inside = _pointerSeen && p.x >= 0 && p.y >= 0 && p.x < size.x && p.y < size.y ? 1.0f : 0.0f;
    const float values[3] = {p.x, p.y, inside};
    std::memcpy(out.data, values, sizeof(values));
  });
  s.bind("__jmRendererSetClearColor", [this](float r, float g, float b, float a) {
    _renderer.setClearColor(glm::vec4(r, g, b, a));
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
  s.bind("__jmSpriteSetTexture", [this, &app](EntityId entity, std::string reference) {
    auto set = [this, &app, entity, reference]() { setSpriteTexture(app.getWorld(), entity, reference); };
    if (app.getWorld().getComponent<SpriteComponent>(entity)) set();
    else app.getSpawner().whenSpawned(entity, set);  // spawned this frame: once it exists
  });
  s.bind("__jmSpriteFinished", [&app](EntityId entity) {
    auto* anim = app.getWorld().getComponent<SpriteAnimationComponent>(entity);
    return anim && anim->finished;
  });
}

void Renderer2DModule::setSpriteTexture(World& world, EntityId entity, const std::string& reference) {
  auto* sprite = world.getComponent<SpriteComponent>(entity);
  if (!sprite || !setSpriteImage(*sprite, reference)) return;
  if (auto* anim = world.getComponent<SpriteAnimationComponent>(entity)) anim->current.clear();  // stop animating over it
}

bool Renderer2DModule::setSpriteImage(SpriteComponent& sprite, const std::string& reference) {
  const auto image = resolveImage(reference);
  if (!image) {
    JM_REPORT_ERROR((ErrorSource{reference}), "[Renderer2D] sprite texture '{}' not found", reference);
    return false;
  }
  sprite.texture = image->texture;
  sprite.texRect = image->texRect;
  return true;
}

nlohmann::json Renderer2DModule::pointerCommand(Engine& app, std::string_view verb, std::string_view args) {
  // The driver's mouse, as the window's would be: events on the bus (so a
  // recorded session keeps them), at logical px from the game's top-left
  // (what UI rects in the state say).
  std::istringstream in{std::string(args)};
  EventBus& bus = app.getEventBus();
  if (verb == "wheel") {
    float dy = 0.0f, dx = 0.0f;
    if (!(in >> dy)) return {{"ok", false}, {"error", "wheel takes an amount, e.g. wheel -1 (down)"}};
    in >> dx;
    bus.emit(EVT_MouseWheel, events::MouseWheel{dx, dy});
    return {{"ok", true}};
  }
  float x = 0.0f, y = 0.0f;
  std::string button = "left";
  const bool at = static_cast<bool>(in >> x >> y);
  if (!at) {
    in.clear();
    in.seekg(0);
  }
  in >> button;
  if (verb == "move" && !at) return {{"ok", false}, {"error", "move takes x y in logical px, e.g. move 120 80"}};
  const int index = button == "left" ? 0 : button == "right" ? 1 : button == "middle" ? 2 : -1;
  if (index < 0) return {{"ok", false}, {"error", "button is left, right or middle"}};
  if (at) {
    const glm::vec2 p = letterbox::toFramebuffer({x, y}, _renderer.gameViewport(), _renderer.frameSize().y,
                                                 _renderer.logicalSize().x);
    bus.emit(EVT_MouseMove, events::MouseMove{p.x, p.y});
  }
  if (verb == "click" || verb == "mousedown") bus.emit(EVT_MouseButton, events::MouseButton{index, true});
  if (verb == "mouseup") bus.emit(EVT_MouseButton, events::MouseButton{index, false});
  if (verb == "click") _pendingRelease = index;  // up a frame later, as a real click
  return {{"ok", true}};
}

void Renderer2DModule::writeCaptures(Engine& app) {
  // Captures asked for since the last frame was drawn show that frame: it's
  // still the final image until the next is drawn. (Fast-forwarding draws
  // nothing: there's no image to give.)
  for (const Engine::CaptureRequest& request : app.takeCaptureRequests()) {
    if (_renderer.gpu() && !app.fastForwarding()) writeImageLater(request.path, request.maxWidth);
  }
}

void Renderer2DModule::tickMainThread(Engine& app, float dt) {
  writeCaptures(app);
  if (_pendingRelease >= 0) {
    app.getEventBus().emit(EVT_MouseButton, events::MouseButton{_pendingRelease, false});
    _pendingRelease = -1;
  }
  _renderer.setTime(static_cast<float>(app.getClock().unscaledElapsed()));
  glm::vec2 shake(0.0f);
  if (_shakeRemaining > 0.0f) {
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    shake = glm::vec2(unit(_shakeRng), unit(_shakeRng)) * _shakeAmplitude * (_shakeRemaining / _shakeDuration);
    _shakeRemaining -= dt;
  }
  if (_editorView) {
    _renderer.camera().setPosition(_editorView->center);
    _renderer.camera().setZoom(_editorView->zoom);
  } else {
    _renderer.camera().setPosition(_cameraBase + shake);
    _renderer.camera().setZoom(_cameraZoom);
  }
  for (auto& pass : _overlayPasses) pass(_renderer);
  _renderer.setDrawing(!app.fastForwarding());
  _renderer.endFrame();
  captureIfRequested(app);
  ++_frame;
}

std::optional<Renderer2DModule::UiPlacement> Renderer2DModule::uiPlacement() const {
  const glm::vec2 logical(_renderer.logicalSize());
  if (!_editorView) return UiPlacement{{}, logical};
  if (!_editorView->showUi || _editorView->gameSize.x <= 0) return std::nullopt;
  // The world origin on the canvas, then the game frame's top-left corner (y down).
  const glm::vec2 game(_editorView->gameSize);
  const glm::vec2 origin = logical * 0.5f + glm::vec2(-_editorView->center.x, _editorView->center.y) * _editorView->zoom;
  return UiPlacement{{origin - game * 0.5f * _editorView->zoom, _editorView->zoom}, game};
}

bool Renderer2DModule::showPostEffect(std::string_view source, std::string& error) {
  const ShaderHandle shader = _renderer.resources().createPostShader(source, "(editor)", &error);
  if (!shader.isValid()) return false;
  PostEffectChain& chain = _renderer.chain();
  PostEffect effect;
  effect.shader = shader;
  if (const PostEffect* old = chain.get(_authoredEffect)) effect.uniforms = old->uniforms;  // keep the sliders' values
  chain.remove(_authoredEffect);
  _renderer.resources().release(_authoredShader);  // recompiled on every edit
  _authoredShader = shader;
  // Transitions blend from u_aux (the outgoing scene): black stands in for it.
  static const uint8_t kBlack[4] = {0, 0, 0, 255};
  if (!_blackTexture.isValid()) _blackTexture = _renderer.resources().createTexture(1, 1, kBlack);
  effect.auxTexture = _blackTexture;
  _authoredEffect = chain.add(std::move(effect));
  error.clear();
  return true;
}

void Renderer2DModule::setPostEffectUniform(const std::string& name, UniformValue value) {
  _renderer.chain().setUniform(_authoredEffect, name, value);
}

void Renderer2DModule::setEditorView(std::optional<EditorView> view) {
  _editorView = view;
  _renderer.setLogicalSizeOverride(view ? std::optional(view->logicalSize) : std::nullopt);
  if (!view) _renderer.camera().setZoom(_cameraZoom);
}

void Renderer2DModule::captureIfRequested(const Engine& app) {
  const DevOptions& dev = app.getDevOptions();
  if (dev.captureDir.empty() ||
      std::find(dev.captureFrames.begin(), dev.captureFrames.end(), _frame) == dev.captureFrames.end()) {
    return;
  }
  if (!_renderer.gpu()) {
    JM_LOG_WARN("[Renderer2D] frame {} not captured: JM_RENDERER=none draws no pixels (JM_DUMP_DIR has the draw list)",
                _frame);
    return;
  }
  std::filesystem::create_directories(dev.captureDir);
  char name[32];
  std::snprintf(name, sizeof(name), "frame_%05llu.png", static_cast<unsigned long long>(_frame));
  writeFrame((dev.captureDir / name).string());
}

bool Renderer2DModule::writeFrame(const std::string& path) {
  if (!writeImage(path)) return false;
  JM_LOG_INFO("[Renderer2D] captured {}", path);
  return true;
}

namespace {

// Scales pixels (RGBA) down by a whole factor to at most maxWidth wide (a box
// filter: a thumbnail, not a frame) and writes them as path's type (.jpg, else
// PNG). Touches no GL: safe off the main thread.
bool encodeImage(std::vector<uint8_t> pixels, int w, int h, const std::filesystem::path& path, int maxWidth) {
  const int factor = maxWidth > 0 ? std::max(1, (w + maxWidth - 1) / maxWidth) : 1;
  if (factor > 1) {
    const int sw = w / factor, sh = h / factor;
    std::vector<uint8_t> small(static_cast<size_t>(sw) * sh * 4);
    for (int y = 0; y < sh; ++y) {
      for (int x = 0; x < sw; ++x) {
        for (int c = 0; c < 4; ++c) {
          int sum = 0;
          for (int dy = 0; dy < factor; ++dy) {
            for (int dx = 0; dx < factor; ++dx) {
              sum += pixels[((static_cast<size_t>(y) * factor + dy) * w + (x * factor + dx)) * 4 + c];
            }
          }
          small[(static_cast<size_t>(y) * sw + x) * 4 + c] = static_cast<uint8_t>(sum / (factor * factor));
        }
      }
    }
    pixels = std::move(small);
    w = sw, h = sh;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::string file = path.string();
  const bool ok = path.extension() == ".jpg" ? stbi_write_jpg(file.c_str(), w, h, 4, pixels.data(), 80) != 0
                                              : stbi_write_png(file.c_str(), w, h, 4, pixels.data(), w * 4) != 0;
  if (!ok) JM_LOG_ERROR("[Renderer2D] couldn't write {}", file);
  return ok;
}

}  // namespace

bool Renderer2DModule::writeImage(const std::filesystem::path& path, int maxWidth) {
  int w = 0, h = 0;
  std::vector<uint8_t> pixels = _renderer.readFinalFrame(w, h);
  return w > 0 && encodeImage(std::move(pixels), w, h, path, maxWidth);
}

void Renderer2DModule::writeImageLater(const std::filesystem::path& path, int maxWidth) {
  // The read back stays here (GL); scaling, encoding and the file don't hold
  // up the frame (a recorded play's thumbnail every second).
  int w = 0, h = 0;
  std::vector<uint8_t> pixels = _renderer.readFinalFrame(w, h);
  if (w <= 0) return;
  std::erase_if(_writes, [](std::future<bool>& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
  _writes.push_back(std::async(std::launch::async, encodeImage, std::move(pixels), w, h, path, maxWidth));
}

bool Renderer2DModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if (verb == "move" || verb == "click" || verb == "mousedown" || verb == "mouseup" || verb == "wheel") {
    reply = pointerCommand(app, verb, args);
    return true;
  }
  if (verb != "capture") return false;
  if (!_renderer.gpu()) {
    reply = {{"ok", false}, {"error", "no pixels with JM_RENDERER=none (state has the draw list)"}};
  } else if (args.empty()) {
    reply = {{"ok", false}, {"error", "capture takes a path, e.g. capture /tmp/frame.png"}};
  } else if (writeFrame(std::string(args))) {
    reply = {{"ok", true}, {"path", args}};
  } else {
    reply = {{"ok", false}, {"error", "couldn't write " + std::string(args)}};
  }
  return true;
}

namespace {

double tidy(float value) { return std::round(static_cast<double>(value) * 100.0) / 100.0; }

}  // namespace

void Renderer2DModule::describeState(Engine&, nlohmann::json& state) {
  auto item = [this](const Renderer2D::DrawItem& d, bool screen) {
    const glm::mat4& m = d.instance.transform;
    const glm::vec2 center(m[3].x, m[3].y);
    const glm::vec2 half(glm::length(glm::vec2(m[0])), glm::length(glm::vec2(m[1])));
    nlohmann::json out = nlohmann::json::object();
    // A loaded image's path; "white" for a sprite with no texture (a solid
    // quad in its color); otherwise the engine's own (glyphs): its id.
    auto path = _imagePaths.find(d.texture.id);
    out["image"] = path != _imagePaths.end()             ? nlohmann::json(path->second)
                   : d.texture == _renderer.whiteTexture() ? nlohmann::json("white")
                                                           : nlohmann::json(d.texture.id);
    if (screen) {  // logical px, from the top-left
      out["rect"] = {tidy(center.x - half.x), tidy(center.y - half.y), tidy(half.x * 2), tidy(half.y * 2)};
    } else {  // world units: center, full size, turn
      out["center"] = {tidy(center.x), tidy(center.y)};
      out["size"] = {tidy(half.x * 2), tidy(half.y * 2)};
      if (const float turn = std::atan2(m[0].y, m[0].x); std::abs(turn) > 1e-4f) out["rotation"] = tidy(turn);
      out["z"] = tidy(d.z);
    }
    const glm::vec4& c = d.instance.color;
    if (c != glm::vec4(1.0f)) out["color"] = {tidy(c.r), tidy(c.g), tidy(c.b), tidy(c.a)};
    const glm::vec4& r = d.instance.texRect;
    if (r != glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)) out["texRect"] = {tidy(r.x), tidy(r.y), tidy(r.z), tidy(r.w)};
    return out;
  };
  nlohmann::json world = nlohmann::json::array(), screen = nlohmann::json::array();
  for (const auto& d : _renderer.drawnWorld()) world.push_back(item(d, false));
  for (const auto& d : _renderer.drawnScreen()) screen.push_back(item(d, true));
  state["draw"] = {{"world", std::move(world)}, {"screen", std::move(screen)}};
}

void Renderer2DModule::shutdown(Engine& app) {
  writeCaptures(app);  // the last frame's (a marker as the play quits): no next frame will
  for (std::future<bool>& write : _writes) write.wait();  // a play's last thumbnails land
  _writes.clear();
  _renderer.shutdown();
  JM_LOG_INFO("[Renderer2D] shutdown");
}
