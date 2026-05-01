#include "Renderer2DModule.hpp"

//
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
//

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "Renderer2DHostFunctions.hpp"
#include "Renderer2DSystem.hpp"
#include "SpriteComponent.hpp"

// Window specific
#include "../glfw_window/WindowEvents.hpp"

// Renderer2D needs an active OpenGL context to load GL function pointers via
// glad. GLFWWindowModule provides that.
template <> struct ModuleTraits<Renderer2DModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<OpenGLContextTag>;
};

REGISTER_MODULE(Renderer2DModule)

void Renderer2DModule::initialize(Engine &app) {
  int width = 1280;
  int height = 720;
  const auto &config = app.getManifest().config;
  if (config.contains("window")) {
    const auto &win = config["window"];
    width = win.value("width", width);
    height = win.value("height", height);
  }
  if (!_renderer.initialize(width, height)) {
    throw std::runtime_error("gladLoadGLLoader failed");
  }

  // Set up Asset handling — decoded texture indexed by the same AssetHandle
  // the AssetManager issued for the raw PNG/JPG bytes. stb_image's
  // load_from_memory takes raw bytes either way, so the same callback works
  // for folder-mode (extension dispatch) and archive-mode (resolver type).
  auto imageDecoder = [this](const RawAsset &asset, const AssetHandle &assetHandle) {
    int w, h, comp;

    stbi_set_flip_vertically_on_load(0);
    const stbi_uc *src = asset.data.empty() ? nullptr : asset.data.data();
    if (!src) {
      JM_LOG_ERROR("[Texture] Empty asset data for '{}'",
                   asset.filePath.string());
      return;
    }

    stbi_uc *pixels =
        stbi_load_from_memory(src, static_cast<int>(asset.data.size()), &w,
                              &h, &comp, STBI_rgb_alpha);

    if (!pixels) {
      JM_LOG_ERROR("[Texture] stb_image failed for '{}': {}",
                   asset.filePath.string(), stbi_failure_reason());
      return;
    }

    auto texHandle = _renderer.createTexture(w, h, 4, pixels);
    stbi_image_free(pixels);

    if (!texHandle.isValid()) {
      JM_LOG_ERROR("[Texture] GL createTexture failed for '{}'",
                   asset.filePath.string());
      return;
    }

    _textures.insert(assetHandle, texHandle);
  };
  app.getAssetManager().addAssetConverter({".png", ".jpg", ".jpeg"}, imageDecoder);
  app.getAssetManager().addAssetTypeConverter("image", imageDecoder);

  // Set up Event handling
  auto &events = app.getEventBus();
  _tResize = events.subscribe<events::WindowResized>(
      EVT_WindowResize, [this](const events::WindowResized e) {
        _renderer.resizeTargets(e.width, e.height);
      });

  // Pre-compile built-in post-effect shaders on the main thread where the GL
  // context is current. Scripts call addBuiltin from worker threads; doing GL
  // work there would segfault. Cached handles make addBuiltin a pure lookup.
  using posteffects::builtins::blur_fragment_shader;
  using posteffects::builtins::color_shift_fragment_shader;
  using posteffects::builtins::crossfade_fragment_shader;
  using posteffects::builtins::grayscale_fragment_shader;
  using posteffects::builtins::pixelate_fragment_shader;
  _builtinShaders[static_cast<size_t>(BuiltinEffectId::Passthrough)] =
      _renderer.createShader(screen_vertex_shader, screen_fragment_shader);
  _builtinShaders[static_cast<size_t>(BuiltinEffectId::Grayscale)] =
      _renderer.createShader(screen_vertex_shader, grayscale_fragment_shader);
  _builtinShaders[static_cast<size_t>(BuiltinEffectId::Blur)] =
      _renderer.createShader(screen_vertex_shader, blur_fragment_shader);
  _builtinShaders[static_cast<size_t>(BuiltinEffectId::Pixelate)] =
      _renderer.createShader(screen_vertex_shader, pixelate_fragment_shader);
  _builtinShaders[static_cast<size_t>(BuiltinEffectId::ColorShift)] =
      _renderer.createShader(screen_vertex_shader, color_shift_fragment_shader);
  _builtinShaders[static_cast<size_t>(BuiltinEffectId::Crossfade)] =
      _renderer.createShader(screen_vertex_shader, crossfade_fragment_shader);

  // Scripting: expose the post-effect chain to scripts.
  setRenderer2DHostContext(app, *this);
  auto &scripts = app.getScriptManager();
  scripts.registerHostFunction(
      "__jmRendererAddBuiltin",
      {"env", "__jmRendererAddBuiltin", "i(i)", &jmRendererAddBuiltin});
  scripts.registerHostFunction(
      "__jmRendererRemoveEffect",
      {"env", "__jmRendererRemoveEffect", "v(i)", &jmRendererRemoveEffect});
  scripts.registerHostFunction(
      "__jmRendererSetEffectEnabled",
      {"env", "__jmRendererSetEffectEnabled", "v(ii)",
       &jmRendererSetEffectEnabled});
  scripts.registerHostFunction(
      "__jmRendererSetEffectUniformFloat",
      {"env", "__jmRendererSetEffectUniformFloat", "v(iiif)",
       &jmRendererSetEffectUniformFloat});
  scripts.registerHostFunction(
      "__jmRendererSetEffectUniformVec3",
      {"env", "__jmRendererSetEffectUniformVec3", "v(iiifff)",
       &jmRendererSetEffectUniformVec3});
  scripts.registerHostFunction(
      "__jmRendererEffectCount",
      {"env", "__jmRendererEffectCount", "i()", &jmRendererEffectCount});

  // Set up ECS
  app.getWorld().registerSystem<Renderer2DSystem>(_renderer);
  app.getWorld().registerComponent<SpriteComponent, PODSpriteComponent>(
      [&](World &world, EntityId id, const nlohmann::json &json) {
        SpriteComponent comp;

        if (json.contains("texture") && json["texture"].is_string()) {
          std::string texName = json["texture"].get<std::string>();
          // Resolve name -> AssetHandle -> TextureHandle. loadAsset is
          // idempotent (dedupes by path), so this is cheap whether the
          // texture was preloaded or is being referenced for the first time.
          try {
            AssetHandle texAsset = app.getAssetManager().loadAsset(texName);
            const TextureHandle *tex = _textures.get(texAsset);
            if (tex && tex->isValid()) {
              comp.texture = *tex;
            } else {
              JM_LOG_ERROR("[SpriteComponent] texture decode missing for: {}",
                           texName);
              comp.texture = _renderer.getDefaultTexture();
            }
          } catch (const std::exception &e) {
            JM_LOG_ERROR("[SpriteComponent] texture load failed for '{}': {}",
                         texName, e.what());
            comp.texture = _renderer.getDefaultTexture();
          }
        }

        if (json.contains("color") && json["color"].is_array()) {
          std::array<float, 4> colorData =
              json["color"].get<std::array<float, 4>>();
          glm::vec4 color{colorData[0], colorData[1], colorData[2],
                          colorData[3]};
          comp.color = color;
        }

        if (json.contains("texRect") && json["texRect"].is_array()) {
          std::array<float, 4> texRectData =
              json["texRect"].get<std::array<float, 4>>();
          glm::vec4 texRect{texRectData[0], texRectData[1], texRectData[2],
                            texRectData[3]};
          comp.texRect = texRect;
        }

        if (json.contains("layer") && json["layer"].is_array()) {
          float layer = json["layer"].get<float>();
          comp.layer = layer;
        }

        world.addComponent<SpriteComponent>(id, comp);
      },
      [&](const World &world, EntityId id, nlohmann::json &out) {
        auto comp = world.getComponent<SpriteComponent>(id);
        if (!comp) {
          return false;
        }

        float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        color[0] = comp->color[0];
        color[1] = comp->color[1];
        color[2] = comp->color[2];
        color[3] = comp->color[3];
        float texRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        texRect[0] = comp->texRect[0];
        texRect[1] = comp->texRect[1];
        texRect[2] = comp->texRect[2];
        texRect[3] = comp->texRect[3];

        out["color"] = color;
        out["texRect"] = texRect;
        out["layer"] = comp->layer;

        // @TODO(asset-path-roundtrip): source texture path not retained on
        // the component. See AssetManager.hpp "Known limitation".

        return true;
      },
      // Deserialize POD data
      [&](World &world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODSpriteComponent))
          return false;

        auto comp = world.getComponent<SpriteComponent>(id);
        if (!comp) {
          return false;
        }

        PODSpriteComponent pod{};
        std::memcpy(&pod, in.data(), sizeof(pod));

        comp->color[0] = pod.cr;
        comp->color[1] = pod.cg;
        comp->color[2] = pod.cb;
        comp->color[3] = pod.ca;
        comp->texRect[0] = pod.tx;
        comp->texRect[1] = pod.ty;
        comp->texRect[2] = pod.tu;
        comp->texRect[3] = pod.tv;
        comp->layer = pod.layer;

        return true;
      },
      // Serialize POD data
      [&](const World &world, EntityId id, std::span<std::byte> out,
          size_t &written) {
        if (out.size() < sizeof(PODSpriteComponent))
          return false;

        const auto *comp = world.getComponent<SpriteComponent>(id);
        if (!comp)
          return false;

        PODSpriteComponent pod{
            comp->color[0],   comp->color[1],   comp->color[2],
            comp->color[3],   comp->texRect[0], comp->texRect[1],
            comp->texRect[2], comp->texRect[3], comp->layer,
        };

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);
        return true;
      });

  // Set up Scripts

  JM_LOG_INFO("[Renderer2DModule] initialized");
}

void Renderer2DModule::tickMainThread(Engine &app, float dt) {
  // Poll SceneManager for transition state. CRITICAL: this block MUST run
  // BEFORE _renderer.endFrame(), not after. endFrame() clears _sceneSurface
  // and renders the new scene's entities; capturing after that would snapshot
  // the new scene (a visual no-op when crossfaded with itself). The FBO color
  // attachment persists between frames, so at the top of tickMainThread
  // _sceneSurface still holds the OUTGOING scene's last-drawn frame — exactly
  // what the snapshot needs to be. See the plan's "Frame-by-frame timing for
  // transitions" subsection for the full timeline.
  //
  // u_progress direction: Crossfade does mix(primary, aux, u_progress), with
  // primary = live new scene, aux = snapshot of old scene. We want u_progress=1
  // at the start (showing aux=old) → u_progress=0 at the end (showing primary
  // =new). state.progress runs 0→1 over the duration, so we push
  // (1.0 - state.progress). If demo smoke (D.6) shows it backwards, flip to
  // state.progress directly.
  const auto &state = app.getSceneManager().getTransitionState();

  if (state.active && !_transitionLive) {
    _transitionSnapshot = _renderer.captureSceneFrame();
    _transitionEffect = addBuiltin(BuiltinEffectId::Crossfade);
    if (_transitionEffect.isValid() && _transitionSnapshot.isValid()) {
      setEffectAuxTexture(_transitionEffect, _transitionSnapshot);
    }
    _transitionLive = true;
  }

  if (state.active && _transitionLive && _transitionEffect.isValid()) {
    const float u = 1.0f - state.progress;
    setEffectUniform(_transitionEffect, "u_progress", u);
  }

  if (!state.active && _transitionLive) {
    if (_transitionEffect.isValid()) {
      removeEffect(_transitionEffect);
      _transitionEffect = {};
    }
    if (_transitionSnapshot.isValid()) {
      _renderer.releaseCapturedTexture(_transitionSnapshot);
      _transitionSnapshot = {};
    }
    _transitionLive = false;
  }

  _renderer.endFrame();
}

void Renderer2DModule::shutdown(Engine &app) {
  if (_tResize) {
    app.getEventBus().unsubscribe(_tResize);
  }
  clearRenderer2DHostContext();
  _renderer.shutdown();
  JM_LOG_INFO("[Renderer2DModule] shutdown");
}

PostEffectHandle Renderer2DModule::addEffect(PostEffect effect) {
  return _renderer.chain().add(std::move(effect));
}

PostEffectHandle Renderer2DModule::addBuiltin(BuiltinEffectId id) {
  // Pure lookup — no GL calls here. Shaders were compiled at initialize time.
  // Scripts reach this via host functions on worker threads, so GL work would
  // race with the main thread.
  const size_t idx = static_cast<size_t>(id);
  if (idx >= _builtinShaders.size() || !_builtinShaders[idx].isValid()) {
    JM_LOG_ERROR("[Renderer2DModule] addBuiltin: unknown or uncompiled BuiltinEffectId");
    return {};
  }

  PostEffect effect;
  effect.shader = _builtinShaders[idx];

  // Default uniforms for parameterised effects.
  switch (id) {
  case BuiltinEffectId::Blur:
    effect.uniforms["u_radius"] = 2.0f;
    break;
  case BuiltinEffectId::Pixelate:
    effect.uniforms["u_pixelSize"] = 4.0f;
    break;
  case BuiltinEffectId::ColorShift:
    effect.uniforms["u_hsvDelta"] = glm::vec3(0.0f);
    break;
  case BuiltinEffectId::Crossfade:
    effect.uniforms["u_progress"] = 0.0f;
    break;
  default:
    break;
  }

  return _renderer.chain().add(std::move(effect));
}

void Renderer2DModule::removeEffect(PostEffectHandle handle) {
  _renderer.chain().remove(handle);
}

void Renderer2DModule::setEffectEnabled(PostEffectHandle handle, bool enabled) {
  _renderer.chain().setEnabled(handle, enabled);
}

void Renderer2DModule::setEffectUniform(PostEffectHandle handle,
                                        std::string_view name,
                                        UniformValue value) {
  _renderer.chain().setUniform(handle, name, std::move(value));
}

void Renderer2DModule::setEffectAuxTexture(PostEffectHandle handle,
                                           TextureHandle tex) {
  _renderer.chain().setAuxTexture(handle, tex);
}

void Renderer2DModule::moveEffect(PostEffectHandle handle, size_t newIndex) {
  _renderer.chain().moveTo(handle, newIndex);
}

size_t Renderer2DModule::effectCount() const {
  return _renderer.chain().size();
}

TextureHandle Renderer2DModule::captureSceneFrame() {
  return _renderer.captureSceneFrame();
}

void Renderer2DModule::releaseCapturedTexture(TextureHandle handle) {
  _renderer.releaseCapturedTexture(handle);
}
