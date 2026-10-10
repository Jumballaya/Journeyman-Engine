#include "HostedEngine.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Project.hpp"
#include "core/app/WindowEvents.hpp"

std::unique_ptr<HostedEngine> HostedEngine::create(const std::filesystem::path& buildDir, const Options& options,
                                                   std::string& error) {
  if (!std::filesystem::exists(buildDir / ".jm.json")) {
    error = "The project hasn't been built yet.";
    return nullptr;
  }
  EngineOptions engineOptions;
  engineOptions.dev = DevOptions{};  // the editor's own JM_* variables are not the game's
  engineOptions.dev.saveDir = options.saveDir;
  engineOptions.dev.entryScene = options.entryScene;
  engineOptions.dev.recordDir = options.recordDir;
  engineOptions.dev.watch = options.watch;
  engineOptions.embedded = true;
  engineOptions.loadEntryScene = !options.entryScene.empty();

  auto hosted = std::unique_ptr<HostedEngine>(new HostedEngine());
  try {
    hosted->_engine = std::make_unique<Engine>(buildDir, ".jm.json", engineOptions);
    hosted->_engine->setSimulating(options.simulate);
    hosted->_engine->initialize();
    hosted->_renderer = hosted->_engine->getModules().find<Renderer2DModule>();
    if (!hosted->_renderer) error = "The engine has no renderer.";
  } catch (const std::exception& e) {
    error = e.what();
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!hosted->_renderer) return nullptr;
  return hosted;
}

std::unique_ptr<HostedEngine> HostedEngine::createPreview(const std::filesystem::path& buildDir, std::string& error) {
  return create(buildDir, {false, "", settingsDir() / "preview-saves"}, error);
}

glm::ivec2 HostedEngine::gameSize() const {
  const nlohmann::json& config = _engine->getManifest().config;
  const auto renderer = config.value("renderer", nlohmann::json::object());
  const auto window = config.value("window", nlohmann::json::object());
  // A logical size of 0 means "the window's", as the engine reads it.
  auto side = [&](const char* logical, const char* windowed, int fallback) {
    const int n = renderer.value(logical, 0);
    return n > 0 ? n : std::max(1, window.value(windowed, fallback));
  };
  return {side("logicalWidth", "width", 1280), side("logicalHeight", "height", 720)};
}

HostedEngine::~HostedEngine() {
  if (_engine) _engine->shutdown();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned HostedEngine::frame(int width, int height, float dt) {
  _engine->resizeView(std::max(width, 1), std::max(height, 1));
  _engine->frame(dt);
  // The engine leaves its own framebuffers and state bound; ImGui draws next.
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_SCISSOR_TEST);
  _texture = _renderer->renderer().frameTexture();
  return _texture;
}

void HostedEngine::key(int key, int scancode, int action) {
  EventBus& bus = _engine->getEventBus();
  if (action == GLFW_PRESS) {
    bus.emit(EVT_KeyDown, events::KeyDown{scancode, key});
    _held.insert({key, scancode});
  }
  if (action == GLFW_RELEASE) {
    bus.emit(EVT_KeyUp, events::KeyUp{scancode, key});
    _held.erase({key, scancode});
  }
  if (action == GLFW_REPEAT) bus.emit(EVT_KeyRepeat, events::KeyRepeat{scancode, key});
}

void HostedEngine::setFocused(bool focused) {
  _engine->setWindowFocused(focused);
  if (focused) return;
  for (const auto& [key, scancode] : _held) _engine->getEventBus().emit(EVT_KeyUp, events::KeyUp{scancode, key});
  _held.clear();
}

void HostedEngine::mouseMove(float x, float y) { _engine->getEventBus().emit(EVT_MouseMove, events::MouseMove{x, y}); }

void HostedEngine::mouseButton(int button, bool down) {
  _engine->getEventBus().emit(EVT_MouseButton, events::MouseButton{button, down});
}

void HostedEngine::mouseWheel(float dx, float dy) { _engine->getEventBus().emit(EVT_MouseWheel, events::MouseWheel{dx, dy}); }
