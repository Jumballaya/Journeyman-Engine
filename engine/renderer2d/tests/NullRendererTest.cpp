#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer2D.hpp"

// Without a GPU the renderer runs the same frame pipeline with no GL calls at
// all (these tests have no context): frames are kept as data.

namespace {

glm::mat4 at(float x, float y) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)); }

}  // namespace

TEST(NullRenderer, KeepsEachFrameSortedBackToFront) {
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  EXPECT_FALSE(renderer.gpu());
  EXPECT_EQ(renderer.logicalSize(), glm::ivec2(320, 180));
  EXPECT_EQ(renderer.gameViewport(), glm::vec4(0, 0, 640, 360));

  const TextureHandle ship = renderer.resources().createTexture(16, 8, nullptr);
  renderer.drawSprite(at(1, 0), glm::vec4(1.0f), {0, 0, 1, 1}, ship, 5.0f);
  renderer.drawSprite(at(2, 0), glm::vec4(1.0f), {0, 0, 1, 1}, {}, 1.0f);
  renderer.drawScreenQuad({10, 20, 30, 40}, glm::vec4(1.0f), {0, 0, 1, 1}, {});
  renderer.endFrame();

  ASSERT_EQ(renderer.drawnWorld().size(), 2u);
  EXPECT_EQ(renderer.drawnWorld()[0].z, 1.0f);  // back first
  EXPECT_EQ(renderer.drawnWorld()[1].texture, ship);
  EXPECT_EQ(renderer.drawnScreen().size(), 1u);
  EXPECT_EQ(renderer.resources().textureSize(ship), glm::vec2(16, 8));

  renderer.endFrame();  // an empty frame replaces the last
  EXPECT_TRUE(renderer.drawnWorld().empty());
  int w = -1, h = -1;
  EXPECT_TRUE(renderer.readFinalFrame(w, h).empty());
  EXPECT_EQ(w, 0);
  renderer.shutdown();
}

TEST(NullRenderer, ResourcesHandOutHandlesWithoutCompiling) {
  GpuResources resources;
  resources.setGpu(false);
  const ShaderHandle shader = resources.createPostShader("not even GLSL", "broken");
  EXPECT_TRUE(shader.isValid());  // nothing compiles, so nothing fails
  EXPECT_EQ(resources.shader(shader), nullptr);
  const TextureHandle page = resources.createEmptyTexture(256, 256, "nearest");
  EXPECT_TRUE(resources.subUploadTexture(page, 0, 0, 4, 4, nullptr));
  resources.release(page);
  EXPECT_FALSE(resources.subUploadTexture(page, 0, 0, 4, 4, nullptr));
}
