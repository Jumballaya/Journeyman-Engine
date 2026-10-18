#include <cstdio>
#include <filesystem>
#include <sstream>
#include <utility>

#include "../../core/app/Engine.hpp"
#include "../../core/app/ModuleTags.hpp"
#include "../../core/app/ModuleTraits.hpp"
#include "../../core/app/Registration.hpp"
#include "../../core/app/WindowEvents.hpp"
#include "../../core/logger/logging.hpp"
#include "../Letterbox.hpp"
#include "../Renderer2DModule.hpp"
#include "PhysicsOverlay.hpp"

// The agent tools' view of the frame (the agent build links this, the player
// doesn't): frames captured as PNGs (JM_CAPTURE_DIR at JM_CAPTURE_FRAMES),
// colliders and terrain drawn over it (JM_DEBUG_PHYSICS, the editor's
// showPhysics), and the driver's mouse, capture and `debug physics`.
class Renderer2DAgentModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  void step(Engine& app, float dt) override;
  void draw(Engine& app, float dt) override;
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;
  const char* name() const override { return "Renderer2DAgentModule"; }

 private:
  Renderer2DModule* _renderer = nullptr;
  int _pendingRelease = -1;       // the driver's click: its button goes up next frame
  bool _debugPhysics = false;     // JM_DEBUG_PHYSICS, or the driver's `debug physics on`
  uint64_t _capturedThrough = 0;  // JM_CAPTURE_FRAMES: frames before this are done

  // The driver's move, click, mousedown, mouseup and wheel commands.
  nlohmann::json pointerCommand(Engine& app, std::string_view verb, std::string_view args);
  bool writeFrame(const std::string& path);  // the last frame as a PNG
};

template <>
struct ModuleTraits<Renderer2DAgentModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag>;
};

REGISTER_MODULE(Renderer2DAgentModule)

void Renderer2DAgentModule::initialize(Engine& app) {
  _renderer = app.getModules().find<Renderer2DModule>();
  if (!_renderer) return;
  _debugPhysics = app.getDevOptions().debugPhysics;
  _renderer->addOverlayPass([this, &app](Renderer2D& renderer) {
    const auto& view = _renderer->editorView();
    if (_debugPhysics || (view && view->showPhysics)) drawPhysicsOverlay(renderer, app.getWorld());
  });
}

void Renderer2DAgentModule::step(Engine& app, float) {
  if (_pendingRelease < 0) return;
  app.getEventBus().emit(EVT_MouseButton, events::MouseButton{_pendingRelease, false});
  _pendingRelease = -1;
}

void Renderer2DAgentModule::draw(Engine& app, float) {
  // Each frame asked for, in the first drawing after it ran (frames are steps: a slow display skips some).
  const uint64_t from = std::exchange(_capturedThrough, app.frameCount());
  const DevOptions& dev = app.getDevOptions();
  for (const uint64_t frame : dev.captureFrames) {
    if (!_renderer || dev.captureDir.empty() || frame < from || frame >= _capturedThrough) continue;
    if (!_renderer->renderer().gpu()) {
      JM_LOG_WARN("[Renderer2D] frame {} not captured: JM_RENDERER=none draws no pixels (JM_DUMP_DIR has the draw list)",
                  frame);
      continue;
    }
    std::filesystem::create_directories(dev.captureDir);
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%05llu.png", static_cast<unsigned long long>(frame));
    writeFrame((dev.captureDir / name).string());
  }
}

bool Renderer2DAgentModule::writeFrame(const std::string& path) {
  if (!_renderer->writeImage(path)) return false;
  JM_LOG_INFO("[Renderer2D] captured {}", path);
  return true;
}

nlohmann::json Renderer2DAgentModule::pointerCommand(Engine& app, std::string_view verb, std::string_view args) {
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
    const Renderer2D& r = _renderer->renderer();
    const glm::vec2 p = letterbox::toFramebuffer({x, y}, r.gameViewport(), r.frameSize().y, r.logicalSize().x);
    bus.emit(EVT_MouseMove, events::MouseMove{p.x, p.y});
  }
  if (verb == "click" || verb == "mousedown") bus.emit(EVT_MouseButton, events::MouseButton{index, true});
  if (verb == "mouseup") bus.emit(EVT_MouseButton, events::MouseButton{index, false});
  if (verb == "click") _pendingRelease = index;  // up a frame later, as a real click
  return {{"ok", true}};
}

bool Renderer2DAgentModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if (!_renderer) return false;
  if (verb == "move" || verb == "click" || verb == "mousedown" || verb == "mouseup" || verb == "wheel") {
    reply = pointerCommand(app, verb, args);
    return true;
  }
  if (verb == "debug") {
    std::istringstream words{std::string(args)};
    std::string what, onOff, extra;
    words >> what >> onOff >> extra;
    if (what != "physics" || (onOff != "on" && onOff != "off") || !extra.empty()) {
      reply = {{"ok", false}, {"error", "debug takes physics on|off: draws colliders and terrain over the frame"}};
    } else {
      _debugPhysics = onOff == "on";
      reply = {{"ok", true}};
    }
    return true;
  }
  if (verb != "capture") return false;
  if (!_renderer->renderer().gpu()) {
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
