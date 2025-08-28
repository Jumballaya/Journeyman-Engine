#include "Renderer2DModule.hpp"

//
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
//

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../app/Application.hpp"
#include "../core/app/Registration.hpp"
#include "Renderer2DSystem.hpp"
#include "SpriteComponent.hpp"

// Window specific
#include "../glfw_window/WindowEvents.hpp"

REGISTER_MODULE(Renderer2DModule)

void Renderer2DModule::initialize(Application& app) {
  // @TODO: Get width and height from app manifest
  int width = 1280;
  int height = 720;
  if (!_renderer.initialize(width, height)) {
    throw std::runtime_error("gladLoadGLLoader failed");
  }

  // Set up Asset handling
  app.getAssetManager().addAssetConverter({".png", ".jpg", ".jpeg"}, [&](const RawAsset& asset, const AssetHandle& assetHandle) {
    int w, h, comp;

    stbi_set_flip_vertically_on_load(0);
    const stbi_uc* src = asset.data.empty() ? nullptr : asset.data.data();
    if (!src) {
      JM_LOG_ERROR("[Texture] Empty asset data for '{}'", asset.filePath.string());
      return;
    }

    stbi_uc* pixels = stbi_load_from_memory(
        src,
        static_cast<int>(asset.data.size()),
        &w,
        &h,
        &comp,
        STBI_rgb_alpha);

    if (!pixels) {
      JM_LOG_ERROR("[Texture] stb_image failed for '{}': {}", asset.filePath.string(), stbi_failure_reason());
      return;
    }

    auto texHandle = _renderer.createTexture(w, h, 4, pixels);
    stbi_image_free(pixels);

    if (!texHandle.isValid()) {
      JM_LOG_ERROR("[Texture] GL createTexture failed for '{}'", asset.filePath.string());
      return;
    }

    const std::string canonical = asset.filePath.generic_string();
    _textureMap[canonical] = texHandle;
  });

  // Set up Event handling
  auto& events = app.getEventBus();
  _tResize = events.subscribe<events::WindowResized>(
      EVT_WindowResize,
      [this](const events::WindowResized e) {
        _renderer.resizeTargets(e.width, e.height);
      });

  // Set up ECS
  app.getWorld().registerSystem<Renderer2DSystem>(_renderer);
  app.getWorld().registerComponent<SpriteComponent>(
      [&](World& world, EntityId id, const nlohmann::json& json) {
        SpriteComponent comp;

        if (json.contains("texture") && json["texture"].is_string()) {
          std::string texName = json["texture"].get<std::string>();
          auto texIt = _textureMap.find(texName);
          if (texIt == _textureMap.end()) {
            JM_LOG_ERROR("[SpriteComponent] texture not found: {}", texName);
            comp.texture = _renderer.getDefaultTexture();
          } else {
            comp.texture = texIt->second;
          }
        }

        if (json.contains("color") && json["color"].is_array()) {
          std::array<float, 4> colorData = json["color"].get<std::array<float, 4>>();
          glm::vec4 color{colorData[0], colorData[1], colorData[2], colorData[3]};
          comp.color = color;
        }

        if (json.contains("texRect") && json["texRect"].is_array()) {
          std::array<float, 4> texRectData = json["texRect"].get<std::array<float, 4>>();
          glm::vec4 texRect{texRectData[0], texRectData[1], texRectData[2], texRectData[3]};
          comp.texRect = texRect;
        }

        if (json.contains("layer") && json["layer"].is_array()) {
          float layer = json["layer"].get<float>();
          comp.layer = layer;
        }

        world.addComponent<SpriteComponent>(id, comp);
      },
      [&](const World& world, EntityId id, nlohmann::json& out) {
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

        // @TODO: Keep a reference the the texture path from deserializing

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODSpriteComponent)) return false;

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
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        if (out.size() < sizeof(PODSpriteComponent)) return false;

        const auto* comp = world.getComponent<SpriteComponent>(id);
        if (!comp) return false;

        PODSpriteComponent pod{
            comp->color[0],
            comp->color[1],
            comp->color[2],
            comp->color[3],
            comp->texRect[0],
            comp->texRect[1],
            comp->texRect[2],
            comp->texRect[3],
            comp->layer,
        };

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);
        return true;
      });

  // Set up Scripts

  JM_LOG_INFO("[Renderer2DModule] initialized");
}

void Renderer2DModule::tickMainThread(Application& app, float dt) {
  _renderer.endFrame();
}

void Renderer2DModule::shutdown(Application& app) {
  if (_tResize) {
    app.getEventBus().unsubscribe(_tResize);
  }
  _renderer.shutdown();
  JM_LOG_INFO("[Renderer2DModule] shutdown");
}
