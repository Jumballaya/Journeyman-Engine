#include "Renderer2DModule.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../app/Application.hpp"
#include "../core/app/Registration.hpp"

REGISTER_MODULE(Renderer2DModule)

void Renderer2DModule::initialize(Application& app) {
  // @TODO: Get width and height from app manifest
  int width = 800;
  int height = 600;
  if (!_renderer.initialize(width, height)) {
    app.abort();
  }

  // Set up Asset handling

  // Set up Event handling

  // Set up ECS

  // Set up Scripts
}

void Renderer2DModule::tickMainThread(Application& app, float dt) {
  _renderer.endFrame();
}

void Renderer2DModule::shutdown(Application& app) {}