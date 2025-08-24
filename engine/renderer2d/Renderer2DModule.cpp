#include "Renderer2DModule.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../app/Application.hpp"
#include "../core/app/Registration.hpp"
#include "Renderer2DSystem.hpp"
#include "SpriteComponent.hpp"

REGISTER_MODULE(Renderer2DModule)

void Renderer2DModule::initialize(Application& app) {
  // @TODO: Get width and height from app manifest
  int width = 1280;
  int height = 720;
  if (!_renderer.initialize(width, height)) {
    throw std::runtime_error("gladLoadGLLoader failed");
  }

  // Set up Asset handling

  // Set up Event handling
  auto& events = app.getEventBus();
  _tResize = events.subscribe<events::WindowResized>(
      [this](const events::WindowResized e) {
        _renderer.resizeTargets(e.width, e.height);
      });

  // Set up ECS
  app.getWorld().registerSystem<Renderer2DSystem>(_renderer);
  app.getWorld().registerComponent<SpriteComponent>(
      [&](World& world, EntityId id, const nlohmann::json& json) {
        SpriteComponent comp;

        // texture
        if (json.contains("texture") && json["texture"].is_string()) {
          // @TODO: Get texture via file name
        }

        // color
        if (json.contains("color") && json["color"].is_array()) {
          std::array<float, 4> colorData = json["color"].get<std::array<float, 4>>();
          glm::vec4 color{colorData[0], colorData[1], colorData[2], colorData[3]};
          comp.color = color;
        }

        // texRect
        if (json.contains("texRect") && json["texRect"].is_array()) {
          std::array<float, 4> texRectData = json["texRect"].get<std::array<float, 4>>();
          glm::vec4 texRect{texRectData[0], texRectData[1], texRectData[2], texRectData[3]};
          comp.texRect = texRect;
        }

        // layer
        if (json.contains("layer") && json["layer"].is_array()) {
          float layer = json["layer"].get<float>();
          comp.layer = layer;
        }

        world.addComponent<SpriteComponent>(id, comp);
      });

  // Set up Scripts

  JM_LOG_INFO("[Renderer2D] initialized");
}

void Renderer2DModule::tickMainThread(Application& app, float dt) {
  _renderer.endFrame();
}

void Renderer2DModule::shutdown(Application& app) {
  if (_tResize) {
    app.getEventBus().unsubscribe(_tResize);
  }
  JM_LOG_INFO("[Renderer2D] shutdown");
}
