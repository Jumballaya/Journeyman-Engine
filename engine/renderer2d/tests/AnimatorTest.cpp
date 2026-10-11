#include <gtest/gtest.h>

#include "../../core/ecs/World.hpp"
#include "../../physics2d/VelocityComponent.hpp"
#include "../AnimatorSystem.hpp"
#include "../SpriteAnimationComponent.hpp"
#include "../SpriteAnimationSystem.hpp"
#include "../SpriteComponent.hpp"

namespace {

using Condition = AnimatorComponent::Condition;

Condition when(std::string_view text) {
  std::string error;
  auto c = Condition::parse(text, error);
  EXPECT_TRUE(c.has_value()) << text << ": " << error;
  return c.value_or(Condition{});
}

// Kage's states: in the air by vertical speed, else running or idle.
struct Fixture {
  World world;
  AnimatorSystem animator;
  SpriteAnimationSystem flipbook;
  EntityId id;

  Fixture() {
    world.registerComponent<AnimatorComponent>();
    world.registerComponent<SpriteAnimationComponent>();
    world.registerComponent<SpriteComponent>();
    world.registerComponent<VelocityComponent>();
    id = world.createEntity();
    world.addComponent<SpriteComponent>(id);
    world.addComponent<VelocityComponent>(id).blocked.y = -1.0f;  // standing
    SpriteAnimationComponent clips;
    for (const char* name : {"idle", "run", "jump", "fall", "hang"}) {
      clips.animations[name].frames = {{TextureHandle{1}, {0, 0, 1, 1}}};
    }
    clips.animations["attack"] = {{{TextureHandle{1}, {0, 0, 1, 1}}, {TextureHandle{1}, {0, 0, 1, 1}}}, 0.1f, false};
    world.addComponent<SpriteAnimationComponent>(id, std::move(clips));
    AnimatorComponent a;
    a.states = {{"attack", when("attack"), true},
                {"hang", when("hanging")},
                {"jump", when("!onGround && vy > 0")},
                {"fall", when("!onGround")},
                {"run", when("speed > 30")},
                {"idle", when("")}};
    world.addComponent<AnimatorComponent>(id, std::move(a));
  }

  VelocityComponent& velocity() { return *world.getComponent<VelocityComponent>(id); }
  AnimatorComponent& params() { return *world.getComponent<AnimatorComponent>(id); }
  std::string tick(float dt = 0.05f) {
    animator.update(world, dt);
    flipbook.update(world, dt);
    return world.getComponent<SpriteAnimationComponent>(id)->current;
  }
};

}  // namespace

TEST(Animator, ConditionsParseParamsComparisonsAndNegation) {
  const Condition c = when(" !onGround && vy >= -2.5 && hurt ");
  ASSERT_EQ(c.tests.size(), 3u);
  EXPECT_EQ(c.tests[0].param, "onGround");
  EXPECT_EQ(c.tests[0].op, Condition::Test::Op::Zero);
  EXPECT_EQ(c.tests[1].op, Condition::Test::Op::GreaterEqual);
  EXPECT_FLOAT_EQ(c.tests[1].value, -2.5f);
  EXPECT_EQ(c.tests[2].op, Condition::Test::Op::NonZero);
  for (const char* bad : {"vy >", "speed > fast", "a || b", "&& a", "3 > x"}) {
    std::string error;
    EXPECT_FALSE(Condition::parse(bad, error).has_value()) << bad;
    EXPECT_FALSE(error.empty());
  }
}

TEST(Animator, ThePoseFollowsTheMotion) {
  Fixture f;
  EXPECT_EQ(f.tick(), "idle");
  f.velocity().velocity = {120.0f, 0.0f};
  EXPECT_EQ(f.tick(), "run");
  f.velocity().blocked.y = 0.0f;
  f.velocity().velocity.y = 300.0f;
  EXPECT_EQ(f.tick(), "jump");
  f.velocity().velocity.y = -10.0f;
  EXPECT_EQ(f.tick(), "fall");
}

TEST(Animator, AScriptsParamsComeFirst) {
  Fixture f;
  f.params().params["hanging"] = 1.0f;
  EXPECT_EQ(f.tick(), "hang");
  f.params().params["speed"] = 50.0f;  // set by a script with its own motion: wins over the velocity's
  f.params().params["hanging"] = 0.0f;
  EXPECT_EQ(f.tick(), "run");
}

TEST(Animator, AHeldStateFinishesItsClipAndATriggerLastsOneFrame) {
  Fixture f;
  f.velocity().velocity = {120.0f, 0.0f};
  f.params().params["attack"] = 1.0f;
  f.params().triggers.push_back("attack");
  EXPECT_EQ(f.tick(), "attack");
  EXPECT_FALSE(f.params().params.contains("attack"));
  EXPECT_EQ(f.tick(), "attack");  // held: on to its last frame
  EXPECT_EQ(f.tick(), "run");
}

TEST(Animator, AHoldWaitsForItsClipButNotForAScriptsOther) {
  Fixture f;
  f.params().params["attack"] = 1.0f;
  EXPECT_EQ(f.tick(), "attack");
  f.params().params["hanging"] = 1.0f;
  f.params().triggers.push_back("hanging");
  f.params().params["attack"] = 0.0f;
  f.world.getComponent<SpriteAnimationComponent>(f.id)->play("fall");  // a script took over the sprite
  EXPECT_EQ(f.tick(), "hang");  // the trigger was still there to read
  f.params().params["attack"] = 1.0f;
  EXPECT_EQ(f.tick(), "attack");
  EXPECT_EQ(f.tick(), "attack");
  EXPECT_EQ(f.tick(), "attack");  // finished, still wanted: again from the top
  EXPECT_EQ(f.world.getComponent<SpriteAnimationComponent>(f.id)->frame, 0u);
}
