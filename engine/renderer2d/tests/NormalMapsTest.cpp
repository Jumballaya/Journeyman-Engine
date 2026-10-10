#include <gtest/gtest.h>

#include <stb_image_write.h>

#include <glm/gtc/matrix_transform.hpp>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"
#include "Renderer2DModule.hpp"

namespace {

void writePixel(const std::filesystem::path& path) {
  const unsigned char pixel[4] = {128, 128, 255, 255};
  stbi_write_png(path.string().c_str(), 1, 1, 4, pixel, 4);
}

}  // namespace

// x.png's normal map is x.normal.png beside it; an atlas's is its normalImage.
TEST(NormalMaps, ResolveImageFindsTheSiblingNormalMap) {
  TempDir game;
  for (const char* name : {"kage.png", "kage.normal.png", "sentry.png", "c.atlas.png", "c.atlas.normal.png"})
    writePixel(game.path() / name);
  game.writeFile("c.atlas.json", R"({"image": "c.atlas.png", "normalImage": "c.atlas.normal.png",
                                     "width": 1, "height": 1, "regions": {"kage": [0, 0, 1, 1]}})");
  game.writeFile(".jm.json", R"({"name": "N", "entryScene": "main.scene.json", "scenes": ["main.scene.json"],
                                 "assets": ["kage.png", "sentry.png", "c.atlas.json"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": []})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  auto& module = *engine.getModules().find<Renderer2DModule>();

  const auto kage = module.resolveImage("kage.png");
  ASSERT_TRUE(kage);
  EXPECT_TRUE(kage->normal.isValid());
  EXPECT_NE(kage->normal, kage->texture);
  EXPECT_FALSE(module.resolveImage("sentry.png")->normal.isValid());
  EXPECT_FALSE(module.resolveImage("kage.normal.png")->normal.isValid());  // a normal map has none of its own
  const auto region = module.resolveImage("c.atlas.json#kage");
  ASSERT_TRUE(region);
  EXPECT_TRUE(region->normal.isValid());

  // Whatever draws the texture (sprites, tiles, particles) draws it with its normal map.
  Renderer2D& renderer = module.renderer();
  renderer.drawSprite(glm::mat4(1.0f), glm::vec4(1.0f), kage->texRect, kage->texture, 0.0f);
  renderer.endFrame();
  ASSERT_EQ(renderer.drawnWorld().size(), 1u);
  EXPECT_EQ(renderer.drawnWorld()[0].normal, kage->normal);
}

TEST(NormalMaps, ASpriteWithoutOneIsUnaffectedAndBatchesApart) {
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(320, 180, RenderSettings{}, /*gpu=*/false));
  const TextureHandle lit = renderer.resources().createTexture(4, 4, nullptr);
  const TextureHandle plain = renderer.resources().createTexture(4, 4, nullptr);
  const TextureHandle normal = renderer.resources().createTexture(4, 4, nullptr);
  renderer.setNormalMap(lit, normal);
  renderer.drawSprite(glm::mat4(1.0f), glm::vec4(1.0f), {0, 0, 1, 1}, plain, 0.0f);
  renderer.drawSprite(glm::mat4(1.0f), glm::vec4(1.0f), {0, 0, 1, 1}, lit, 0.0f);
  renderer.drawSprite(glm::mat4(1.0f), glm::vec4(1.0f), {0, 0, 1, 1}, {}, 0.0f);  // a solid quad
  renderer.endFrame();

  const auto& items = renderer.drawnWorld();
  ASSERT_EQ(items.size(), 3u);
  for (const auto& item : items) EXPECT_EQ(item.normal, item.texture == lit ? normal : TextureHandle{});
  Renderer2D::DrawItem sameTextureNoNormal = items[0];
  sameTextureNoNormal.texture = lit;
  sameTextureNoNormal.normal = {};
  const auto withNormal = std::ranges::find(items, lit, &Renderer2D::DrawItem::texture);
  EXPECT_TRUE(withNormal->batchesWith(*withNormal));
  EXPECT_FALSE(withNormal->batchesWith(sameTextureNoNormal));  // a draw binds one normal map
  renderer.shutdown();
}
