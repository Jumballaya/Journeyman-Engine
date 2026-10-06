#include <gtest/gtest.h>

#include <vector>

#include "World.hpp"

TEST(Hierarchy, ChildrenGoWithTheirParent) {
  World world;
  const EntityId root = world.createEntity(), child = world.createEntity(), grandchild = world.createEntity();
  ASSERT_TRUE(world.setParent(child, root));
  ASSERT_TRUE(world.setParent(grandchild, child));
  EXPECT_EQ(world.parentOf(grandchild), child);
  EXPECT_EQ(world.childrenOf(root), std::vector<EntityId>{child});
  world.destroyEntity(root);
  EXPECT_FALSE(world.isAlive(child));
  EXPECT_FALSE(world.isAlive(grandchild));
}

TEST(Hierarchy, LoopsAreRefusedAndDetachingKeepsTheChild) {
  World world;
  const EntityId a = world.createEntity(), b = world.createEntity();
  ASSERT_TRUE(world.setParent(b, a));
  EXPECT_FALSE(world.setParent(a, b));  // a would be its own ancestor
  EXPECT_FALSE(world.setParent(a, a));
  std::vector<EntityId> heard;
  world.onParentChanged([&](EntityId child, EntityId, World::Attach) { heard.push_back(child); });
  ASSERT_TRUE(world.setParent(b, kNoEntityId));
  EXPECT_EQ(world.parentOf(b), kNoEntityId);
  EXPECT_TRUE(world.childrenOf(a).empty());
  world.destroyEntity(a);
  EXPECT_TRUE(world.isAlive(b));
  EXPECT_EQ(heard, std::vector<EntityId>{b});
}

// A child destroyed alone leaves its parent's list.
TEST(Hierarchy, DestroyingAChildUnlinksIt) {
  World world;
  const EntityId parent = world.createEntity(), child = world.createEntity();
  world.setParent(child, parent);
  world.destroyEntity(child);
  EXPECT_TRUE(world.childrenOf(parent).empty());
  EXPECT_TRUE(world.isAlive(parent));
}
