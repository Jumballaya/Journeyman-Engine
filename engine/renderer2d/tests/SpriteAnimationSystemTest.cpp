#include <gtest/gtest.h>

#include <utility>

#include "../../core/ecs/World.hpp"
#include "../SpriteAnimationComponent.hpp"
#include "../SpriteAnimationSystem.hpp"
#include "../SpriteComponent.hpp"

namespace {

using Frame = SpriteAnimationComponent::Frame;
using Animation = SpriteAnimationComponent::Animation;

// Two frames: the left and right halves of texture 7.
const Frame kA{TextureHandle{7}, {0.0f, 0.0f, 0.5f, 1.0f}};
const Frame kB{TextureHandle{7}, {0.5f, 0.0f, 0.5f, 1.0f}};

struct Fixture {
  World world;
  SpriteAnimationSystem system;
  EntityId id;

  explicit Fixture(std::vector<std::pair<std::string, Animation>> animations, std::string current) {
    world.registerComponent<SpriteAnimationComponent>();
    world.registerComponent<SpriteComponent>();
    id = world.createEntity();
    world.addComponent<SpriteComponent>(id);
    SpriteAnimationComponent anim;
    for (auto& [name, a] : animations) anim.animations.emplace(name, a);
    anim.current = std::move(current);
    world.addComponent<SpriteAnimationComponent>(id, std::move(anim));
  }

  SpriteAnimationComponent& anim() { return *world.getComponent<SpriteAnimationComponent>(id); }
  SpriteComponent& sprite() { return *world.getComponent<SpriteComponent>(id); }
  void tick(float dt) { system.update(world, dt); }
};

Fixture single(bool loop, std::vector<Frame> frames = {kA, kB}) {
  return Fixture({{"default", Animation{std::move(frames), 1.0f, loop}}}, "default");
}

}  // namespace

TEST(SpriteAnimationSystem, AdvancesOnlyAfterFrameDuration) {
  Fixture f = single(true);
  f.tick(0.5f);
  EXPECT_EQ(f.anim().frame, 0u);
  f.tick(0.6f);
  EXPECT_EQ(f.anim().frame, 1u);
}

TEST(SpriteAnimationSystem, LoopingWrapsToFirstFrameAndNeverFinishes) {
  Fixture f = single(true);
  for (int i = 0; i < 5; ++i) {
    f.tick(1.0f);
    EXPECT_FALSE(f.anim().finished);
  }
  EXPECT_EQ(f.anim().frame, 1u);
  f.tick(1.0f);
  EXPECT_EQ(f.anim().frame, 0u);
}

TEST(SpriteAnimationSystem, NonLoopingHoldsLastFrameAndFinishes) {
  Fixture f = single(false);
  f.tick(0.5f);
  EXPECT_FALSE(f.anim().finished);
  f.tick(0.6f);
  EXPECT_TRUE(f.anim().finished);
  f.tick(5.0f);
  EXPECT_EQ(f.anim().frame, 1u);
}

TEST(SpriteAnimationSystem, SingleFrameNonLoopingFinishesImmediately) {
  Fixture f = single(false, {kA});
  f.tick(0.0f);
  EXPECT_TRUE(f.anim().finished);
}

TEST(SpriteAnimationSystem, WritesCurrentFrameIntoSprite) {
  Fixture f = single(true);
  f.tick(0.0f);
  EXPECT_EQ(f.sprite().texture.id, 7u);
  EXPECT_EQ(f.sprite().texRect, kA.texRect);
  f.tick(1.0f);
  EXPECT_EQ(f.sprite().texRect, kB.texRect);
}

TEST(SpriteAnimationSystem, PlayRestartsAndClearsFinished) {
  Fixture f({{"die", Animation{{kA}, 1.0f, false}}, {"idle", Animation{{kA, kB}, 1.0f, true}}}, "die");
  f.tick(0.0f);
  ASSERT_TRUE(f.anim().finished);

  EXPECT_TRUE(f.anim().play("idle"));
  EXPECT_FALSE(f.anim().finished);
  f.tick(0.5f);
  EXPECT_FALSE(f.anim().finished);
  EXPECT_FALSE(f.anim().play("missing"));
  EXPECT_EQ(f.anim().current, "idle");
}

TEST(SpriteAnimationSystem, UnknownCurrentLeavesSpriteAlone) {
  Fixture f = single(true);
  f.anim().current = "nope";
  f.sprite().texture = TextureHandle{3};
  f.tick(1.0f);
  EXPECT_EQ(f.sprite().texture.id, 3u);
}
