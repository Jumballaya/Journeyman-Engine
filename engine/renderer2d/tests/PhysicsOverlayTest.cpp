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
  world.registerComponent<GroundComponent>();
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
  world.addComponent<GroundComponent>(ledge).chains.emplace_back(std::vector<glm::vec2>{{-4, 0}, {4, 0}}, false, true);

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
  EXPECT_EQ(items.front().lit, Renderer2D::Lit::Yes);              // the game's sprite is lit
  EXPECT_EQ(items.back().lit, Renderer2D::Lit::No);                // the overlay reads in the dark
  renderer.shutdown();
}

TEST(PhysicsOverlay, LeavesOutWhatIsBeingDestroyed) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<BoxColliderComponent>();
  world.registerComponent<CircleColliderComponent>();
  world.registerComponent<GroundComponent>();
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
  world.registerComponent<GroundComponent>();
  const EntityId hill = world.createEntity();
  world.addComponent<TransformComponent>(hill).position = {0, 0, 4};
  auto& t = world.addComponent<GroundComponent>(hill);
  t.chains.emplace_back(std::vector<glm::vec2>{{0, 0}, {10, 5}, {20, 0}}, false, false);
  t.strokeColor = {0.4f, 0.8f, 0.3f, 1.0f};
  t.collisionLayer = 0;  // collides with nothing: still drawn
  const EntityId hidden = world.createEntity();
  world.addComponent<TransformComponent>(hidden);
  world.addComponent<GroundComponent>(hidden).chains.emplace_back(std::vector<glm::vec2>{{0, 0}, {5, 0}}, false, false);
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  Renderer2DSystem(renderer).update(world, 0.0f);
  renderer.endFrame();
  ASSERT_EQ(renderer.drawnWorld().size(), 2u);  // the hill's two lines
  EXPECT_EQ(renderer.drawnWorld()[0].z, 4.0f);
  renderer.shutdown();
}

TEST(PhysicsOverlay, DrawsTheShapePhysicsUses) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<BoxColliderComponent>();
  world.registerComponent<CircleColliderComponent>();
  world.registerComponent<GroundComponent>();
  const EntityId dot = world.createEntity();
  world.addComponent<TransformComponent>(dot);
  world.addComponent<CircleColliderComponent>(dot).radius = -8;  // physics: radius 0
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  drawPhysicsOverlay(renderer, world);
  renderer.endFrame();
  EXPECT_TRUE(renderer.drawnWorld().empty());  // no ring around nothing
  renderer.shutdown();
}
