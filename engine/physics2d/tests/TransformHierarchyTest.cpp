#include <gtest/gtest.h>

#include <cmath>

#include "TransformHierarchy.hpp"

namespace {

struct Scene {
  World world;
  Scene() {
    world.registerComponent<TransformComponent>();
    installTransformHierarchy(world);
  }
  EntityId at(float x, float y, float z = 0, float rotation = 0) {
    const EntityId id = world.createEntity();
    auto& t = world.addComponent<TransformComponent>(id);
    t.position = {x, y, z};
    t.rotationRad = rotation;
    return id;
  }
  TransformComponent& transform(EntityId id) { return *world.getComponent<TransformComponent>(id); }
  void frame() {
    world.runSystems(0.016f);
  }
};

}  // namespace

// Authored children are placed from their parent at once, and follow it every frame.
TEST(TransformHierarchy, AuthoredChildrenFollowTheirParent) {
  Scene s;
  const EntityId parent = s.at(100, 50, 2), child = s.at(10, 0, -1), grandchild = s.at(0, 5);
  s.world.setParent(child, parent, World::Attach::AsAuthored);
  s.world.setParent(grandchild, child, World::Attach::AsAuthored);
  EXPECT_EQ(s.transform(grandchild).position, glm::vec3(110, 55, 1));  // z adds up too
  s.transform(parent).position = {0, 0, 2};
  s.transform(parent).rotationRad = glm::pi<float>() / 2;  // a quarter turn: +x points up
  s.frame();
  EXPECT_NEAR(s.transform(child).position.x, 0.0f, 1e-4f);
  EXPECT_NEAR(s.transform(child).position.y, 10.0f, 1e-4f);
  EXPECT_NEAR(s.transform(grandchild).position.x, -5.0f, 1e-4f);
  EXPECT_NEAR(s.transform(grandchild).rotationRad, glm::pi<float>() / 2, 1e-5f);
  EXPECT_EQ(s.transform(child).scale, glm::vec2(1));  // scale is its own
}

// Attaching at run time keeps the child where it is; detaching leaves it there.
TEST(TransformHierarchy, AttachingInPlaceKeepsTheChildStill) {
  Scene s;
  const EntityId parent = s.at(20, 20, 0, 1.0f), child = s.at(30, 25);
  s.world.setParent(child, parent, World::Attach::InPlace);
  EXPECT_NEAR(s.transform(child).position.x, 30.0f, 1e-4f);
  EXPECT_NEAR(s.transform(child).position.y, 25.0f, 1e-4f);
  s.transform(parent).position.x += 5;
  s.frame();
  EXPECT_NEAR(s.transform(child).position.x, 35.0f, 1e-4f);
  s.world.setParent(child, kNoEntityId);
  EXPECT_FALSE(s.world.hasComponent<LocalTransformComponent>(child));
  s.transform(parent).position.x += 5;
  s.frame();
  EXPECT_NEAR(s.transform(child).position.x, 35.0f, 1e-4f);
}
