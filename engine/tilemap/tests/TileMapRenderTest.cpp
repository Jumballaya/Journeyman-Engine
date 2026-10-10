#include <gtest/gtest.h>

#include <stb_image_write.h>

#include "../../core/tests/assets/TempDir.hpp"
#include "../../renderer2d/Renderer2DModule.hpp"
#include "Engine.hpp"

// Shadows fall by world position: a parallax layer, drawn elsewhere, is lit but not shadowed.
TEST(TileMapRender, ParallaxLayersAreUnshadowed) {
  TempDir game;
  const unsigned char pixel[4] = {255, 255, 255, 255};
  stbi_write_png((game.path() / "far.png").string().c_str(), 1, 1, 4, pixel, 4);
  stbi_write_png((game.path() / "near.png").string().c_str(), 1, 1, 4, pixel, 4);
  game.writeFile("level.tmj", R"({"width": 4, "height": 2, "tilewidth": 16, "tileheight": 16, "layers": [
      {"type": "imagelayer", "image": "far.png", "parallaxx": 0.25, "parallaxy": 1},
      {"type": "imagelayer", "image": "near.png"}]})");
  game.writeFile(".jm.json", R"({"name": "T", "entryScene": "main.scene.json", "scenes": ["main.scene.json"],
                                 "assets": ["far.png", "near.png", "level.tmj"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [{"name": "Map", "components": {
      "TransformComponent": {"position": [0, 0, 0]}, "TileMapComponent": {"map": "level.tmj"}}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  engine.frame(1.0f / 60.0f);

  auto& module = *engine.getModules().find<Renderer2DModule>();
  const TextureHandle far = module.resolveImage("far.png")->texture, near = module.resolveImage("near.png")->texture;
  int seen = 0;
  for (const auto& item : module.renderer().drawnWorld()) {
    if (item.texture != far && item.texture != near) continue;
    EXPECT_EQ(item.lit, item.texture == far ? Renderer2D::Lit::Unshadowed : Renderer2D::Lit::Yes);
    ++seen;
  }
  EXPECT_EQ(seen, 2);
  engine.shutdown();
}
