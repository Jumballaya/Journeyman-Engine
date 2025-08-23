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
  int width = 800;
  int height = 600;
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
