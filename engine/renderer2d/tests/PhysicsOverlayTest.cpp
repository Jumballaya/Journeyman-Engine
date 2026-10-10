#include <gtest/gtest.h>

#include "../../physics2d/BoxColliderComponent.hpp"
#include "../../physics2d/CircleColliderComponent.hpp"
#include "../../physics2d/Terrain.hpp"
#include "PhysicsOverlay.hpp"
#include "Renderer2DSystem.hpp"

TEST(PhysicsOverlay, OutlinesCollidersAndDrawsTerrainOnTop) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<BoxColliderComponent>();
  world.registerComponent<CircleColliderComponent>();
  world.registerComponent<TerrainComponent>();
  const EntityId wall = world.createEntity();
  world.addComponent<TransformComponent>(wall).position = {10, 0, 0};
  auto& box = world.addComponent<BoxColliderComponent>(wall);
  box.halfExtents = {2, 2};
  box.blocksMask = 1;
  const EntityId coin = world.createEntity();
  world.addComponent<TransformComponent>(coin);
  world.addComponent<CircleColliderComponent>(coin);
  const EntityId ledge = world.createEntity();
  world.addComponent<TransformComponent>(ledge).position = {0, 5, 0};
  world.addComponent<TerrainComponent>(ledge).chains.emplace_back(std::vector<glm::vec2>{{-4, 0}, {4, 0}}, false, true);

  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  renderer.drawSprite(glm::mat4(1.0f), glm::vec4(1.0f), {0, 0, 1, 1}, {}, 100.0f);  // the game's own sprite
  drawPhysicsOverlay(renderer, world);
  renderer.endFrame();

  const auto& items = renderer.drawnWorld();
  ASSERT_EQ(items.size(), 1u + 4u + 24u + 1u);  // the sprite, the box's sides, the circle, the ledge
  EXPECT_EQ(items.front().z, 100.0f);           // under every line
  const auto& ledgeLine = items.back().instance;
  EXPECT_EQ(glm::vec2(ledgeLine.transform[3]), glm::vec2(0, 5));  // centered on it
  EXPECT_GT(ledgeLine.color.g, ledgeLine.color.b);                // one-way: yellow
  renderer.shutdown();
}

TEST(PhysicsOverlay, LeavesOutWhatIsBeingDestroyed) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<BoxColliderComponent>();
  world.registerComponent<CircleColliderComponent>();
  world.registerComponent<TerrainComponent>();
  const EntityId gone = world.createEntity();
  world.addComponent<TransformComponent>(gone);
  world.addComponent<BoxColliderComponent>(gone).halfExtents = {2, 2};
  world.destroyDeferred(gone);
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  drawPhysicsOverlay(renderer, world);
  renderer.endFrame();
  EXPECT_TRUE(renderer.drawnWorld().empty());
  renderer.shutdown();
}

TEST(PhysicsOverlay, StrokedTerrainIsDrawnAtItsDepthAndPlainTerrainIsnt) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<SpriteComponent>();
  world.registerComponent<ParticleEmitterComponent>();
  world.registerComponent<TerrainComponent>();
  const EntityId hill = world.createEntity();
  world.addComponent<TransformComponent>(hill).position = {0, 0, 4};
  auto& t = world.addComponent<TerrainComponent>(hill);
  t.chains.emplace_back(std::vector<glm::vec2>{{0, 0}, {10, 5}, {20, 0}}, false, false);
  t.strokeColor = {0.4f, 0.8f, 0.3f, 1.0f};
  t.layerMask = 0;  // collides with nothing: still drawn
  const EntityId hidden = world.createEntity();
  world.addComponent<TransformComponent>(hidden);
  world.addComponent<TerrainComponent>(hidden).chains.emplace_back(std::vector<glm::vec2>{{0, 0}, {5, 0}}, false, false);
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  Renderer2DSystem(renderer).update(world, 0.0f);
  renderer.endFrame();
  ASSERT_EQ(renderer.drawnWorld().size(), 2u);  // the hill's two lines
  EXPECT_EQ(renderer.drawnWorld()[0].z, 4.0f);
  renderer.shutdown();
}
