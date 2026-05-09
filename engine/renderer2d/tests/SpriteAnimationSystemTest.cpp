#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>
#include <unordered_map>
#include <utility>

#include "../../core/assets/AssetHandle.hpp"
#include "../../core/ecs/World.hpp"
#include "../AtlasManager.hpp"
#include "../SpriteAnimationComponent.hpp"
#include "../SpriteAnimationSystem.hpp"
#include "../SpriteComponent.hpp"

namespace {

// Helper: register the two components in a fresh World. The POD callbacks are
// dummies — these tests don't exercise round-trip — but using `char` as the
// POD type would set sizeof(POD) = 1 and silently change registration
// semantics if a future test does invoke POD ser/de.
void registerSpriteAnimationComponents(World& world) {
  world.registerComponent<SpriteAnimationComponent, PODSpriteAnimationComponent>(
      [](World&, EntityId, const nlohmann::json&) {},
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return true; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return true; });
  world.registerComponent<SpriteComponent, PODSpriteComponent>(
      [](World&, EntityId, const nlohmann::json&) {},
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return true; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return true; });
}

// Helper: hand-build a single-atlas AtlasManager containing two 32x32 regions
// at (0,0) and (32,0) inside a 64x32 atlas, named "a" and "b".
std::pair<AssetHandle, AtlasManager> makeAtlasWithAB() {
  AtlasManager atlas;
  AssetHandle handle{42};
  TextureHandle tex;
  tex.id = 7;
  std::unordered_map<std::string, std::array<int, 4>> regions{
      {"a", {0, 0, 32, 32}},
      {"b", {32, 0, 32, 32}},
  };
  atlas.loadAtlas(handle, "test.atlas.json", tex, 64, 32, regions);
  return {handle, std::move(atlas)};
}

}  // namespace

// Frame does not advance until elapsed exceeds frameDuration.
TEST(SpriteAnimationSystem, AnimationAdvancesFrameWhenElapsedExceedsFrameDuration) {
  World world;
  registerSpriteAnimationComponents(world);
  auto [handle, atlas] = makeAtlasWithAB();
  SpriteAnimationSystem system(atlas);

  EntityId id = world.createEntity();
  world.addComponent<SpriteComponent>(id);
  SpriteAnimationComponent ac;
  ac.atlasPath = "test.atlas.json";
  ac._atlasHandle = handle;
  ac.current = "default";
  SpriteAnimationComponent::Animation a;
  a.regions = {"a", "b"};
  a.frameDuration = 1.0f;
  a.loop = true;
  ac.animations.emplace("default", a);
  world.addComponent<SpriteAnimationComponent>(id, std::move(ac));

  // dt = 0.5 — elapsed below frameDuration, frame should NOT advance.
  system.update(world, 0.5f);
  EXPECT_EQ(world.getComponent<SpriteAnimationComponent>(id)->frameIndex, 0u);

  // dt = 0.6 — elapsed = 1.1 ≥ 1.0, frame advances to 1.
  system.update(world, 0.6f);
  EXPECT_EQ(world.getComponent<SpriteAnimationComponent>(id)->frameIndex, 1u);
}

// With loop=true, frameIndex wraps to 0 after the last frame.
TEST(SpriteAnimationSystem, AnimationLoopsWhenLoopIsTrue) {
  World world;
  registerSpriteAnimationComponents(world);
  auto [handle, atlas] = makeAtlasWithAB();
  SpriteAnimationSystem system(atlas);

  EntityId id = world.createEntity();
  world.addComponent<SpriteComponent>(id);
  SpriteAnimationComponent ac;
  ac.atlasPath = "test.atlas.json";
  ac._atlasHandle = handle;
  ac.current = "default";
  SpriteAnimationComponent::Animation a;
  a.regions = {"a", "b"};
  a.frameDuration = 1.0f;
  a.loop = true;
  ac.animations.emplace("default", a);
  ac.frameIndex = 1;  // start on the last frame
  world.addComponent<SpriteAnimationComponent>(id, std::move(ac));

  // One full frameDuration tick — wraps from 1 → 0.
  system.update(world, 1.0f);
  EXPECT_EQ(world.getComponent<SpriteAnimationComponent>(id)->frameIndex, 0u);
}

// With loop=false, frameIndex clamps at the last frame.
TEST(SpriteAnimationSystem, AnimationStopsAtLastFrameWhenLoopIsFalse) {
  World world;
  registerSpriteAnimationComponents(world);
  auto [handle, atlas] = makeAtlasWithAB();
  SpriteAnimationSystem system(atlas);

  EntityId id = world.createEntity();
  world.addComponent<SpriteComponent>(id);
  SpriteAnimationComponent ac;
  ac.atlasPath = "test.atlas.json";
  ac._atlasHandle = handle;
  ac.current = "default";
  SpriteAnimationComponent::Animation a;
  a.regions = {"a", "b"};
  a.frameDuration = 1.0f;
  a.loop = false;
  ac.animations.emplace("default", a);
  ac.frameIndex = 1;  // already on the last frame
  world.addComponent<SpriteAnimationComponent>(id, std::move(ac));

  // Several ticks past the last frame's duration — should stay at index 1.
  system.update(world, 1.0f);
  system.update(world, 1.0f);
  system.update(world, 1.0f);
  EXPECT_EQ(world.getComponent<SpriteAnimationComponent>(id)->frameIndex, 1u);
}

// The system writes the current frame's atlas-resolved (texture, texRect)
// into the SpriteComponent in-place.
TEST(SpriteAnimationSystem, AnimationLooksUpRegionViaAtlasManager) {
  World world;
  registerSpriteAnimationComponents(world);
  auto [handle, atlas] = makeAtlasWithAB();
  SpriteAnimationSystem system(atlas);

  EntityId id = world.createEntity();
  world.addComponent<SpriteComponent>(id);
  SpriteAnimationComponent ac;
  ac.atlasPath = "test.atlas.json";
  ac._atlasHandle = handle;
  ac.current = "default";
  SpriteAnimationComponent::Animation a;
  a.regions = {"a", "b"};
  a.frameDuration = 1.0f;
  a.loop = true;
  ac.animations.emplace("default", a);
  world.addComponent<SpriteAnimationComponent>(id, std::move(ac));

  // First tick — frame 0, region "a" at pixel rect [0,0,32,32] in 64x32 atlas
  // → UV [0, 0, 0.5, 1.0].
  system.update(world, 0.0f);
  auto* sprite = world.getComponent<SpriteComponent>(id);
  ASSERT_NE(sprite, nullptr);
  EXPECT_EQ(sprite->texture.id, 7u);
  EXPECT_FLOAT_EQ(sprite->texRect.x, 0.0f);
  EXPECT_FLOAT_EQ(sprite->texRect.y, 0.0f);
  EXPECT_FLOAT_EQ(sprite->texRect.z, 0.5f);
  EXPECT_FLOAT_EQ(sprite->texRect.w, 1.0f);

  // Tick past frameDuration — frame advances to 1, region "b" at [32,0,32,32]
  // → UV [0.5, 0, 0.5, 1.0].
  system.update(world, 1.0f);
  sprite = world.getComponent<SpriteComponent>(id);
  EXPECT_FLOAT_EQ(sprite->texRect.x, 0.5f);
  EXPECT_FLOAT_EQ(sprite->texRect.y, 0.0f);
  EXPECT_FLOAT_EQ(sprite->texRect.z, 0.5f);
  EXPECT_FLOAT_EQ(sprite->texRect.w, 1.0f);
}
