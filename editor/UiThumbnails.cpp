#include "UiThumbnails.hpp"

#include <vector>

#include <glad/gl.h>

#include "HostedEngine.hpp"
#include "LogBook.hpp"
#include "Project.hpp"
#include "editors/UiSource.hpp"
#include "ui/UIModule.hpp"

namespace fs = std::filesystem;

UiThumbnails& UiThumbnails::instance() {
  static UiThumbnails thumbnails;
  return thumbnails;
}

void UiThumbnails::clear() {
  for (auto& [_, shot] : _shots) glDeleteTextures(1, &shot.texture);
  _shots.clear();
  _engine.reset();
}

std::optional<Thumbnails::Picture> UiThumbnails::get(const Project& project, const std::string& path, uint64_t buildGeneration) {
  std::error_code ec;
  const auto modified = fs::last_write_time(project.abs(path), ec);
  if (ec) return std::nullopt;
  // A new build (fonts, images, stylesheets) or another project: start over.
  if (_engineRoot != project.root() || _engineBuild != buildGeneration) {
    clear();
    _engineRoot = project.root();
    _engineBuild = buildGeneration;
  }
  // A failed drawing waits for the file or the build to change, not the next frame.
  Shot& shot = _shots[path];
  if (shot.modified != modified && ImGui::GetFrameCount() != _lastDrawFrame) {
    _lastDrawFrame = ImGui::GetFrameCount();
    shot.modified = modified;
    draw(project, path, shot);
  }
  if (!shot.texture) return std::nullopt;
  // Engine frames are stored bottom row first.
  return Thumbnails::Picture{static_cast<ImTextureID>(shot.texture), {0, 1}, {1, 0}, shot.size};
}

void UiThumbnails::draw(const Project& project, const std::string& path, Shot& shot) {
  MuteEngineLog mute;
  std::string error;
  if (!_engine) _engine = HostedEngine::createPreview(project.buildDir(), error);
  UIModule* ui = _engine ? _engine->engine().getModules().find<UIModule>() : nullptr;
  if (!ui) return;
  const glm::ivec2 game = _engine->gameSize();
  // Stylesheets from the sources: a build in progress may not have them yet.
  const uint32_t document = ui->openDocument(
      uisource::inlineStylesheets(project.readText(path), [&](const std::string& href) { return project.readText(href); }));
  Renderer2DModule::EditorView view;
  view.logicalSize = game;
  view.gameSize = game;
  view.showUi = true;
  _engine->renderer().setEditorView(view);
  // Twice the game's size: crisp at tile size, and pixel art stays sharp.
  const int w = game.x * 2, h = game.y * 2;
  _engine->frame(w, h, 0.0f);  // a fresh engine's first frame comes out empty
  const unsigned frame = _engine->frame(w, h, 0.0f);
  ui->closeDocument(document);

  std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  glBindTexture(GL_TEXTURE_2D, frame);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  if (!shot.texture) glGenTextures(1, &shot.texture);
  glBindTexture(GL_TEXTURE_2D, shot.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  shot.size = ImVec2(static_cast<float>(game.x), static_cast<float>(game.y));
}
