#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "core/app/Engine.hpp"
#include "renderer2d/Renderer2DModule.hpp"

// An Engine running inside the editor on a project's build/ folder: it draws
// into a texture the editor shows, and gets input only when the editor says.
class HostedEngine {
 public:
  struct Options {
    bool simulate = true;          // false = edit preview (render only)
    std::string entryScene;        // empty = load none
    std::filesystem::path saveDir; // where the game's save.json goes
  };

  // nullptr with `error` set when the build is missing or startup fails.
  static std::unique_ptr<HostedEngine> create(const std::filesystem::path& buildDir, const Options& options,
                                              std::string& error);
  ~HostedEngine();

  Engine& engine() { return *_engine; }
  Renderer2DModule& renderer() { return *_renderer; }

  // Advances `dt` seconds and renders at `pixels` (framebuffer size). Returns
  // the frame's GL texture (rows bottom first). dt 0 renders without advancing.
  unsigned frame(int width, int height, float dt);
  unsigned lastTexture() const { return _texture; }

  // GLFW key events for the game (only forwarded while it has focus).
  void key(int key, int scancode, int action);
  void setFocused(bool focused) { _engine->setViewFocused(focused); }

 private:
  std::unique_ptr<Engine> _engine;
  Renderer2DModule* _renderer = nullptr;
  unsigned _texture = 0;
};
