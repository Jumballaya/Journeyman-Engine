#include <gtest/gtest.h>

#include <numbers>
#include <tuple>

#include "../StepBlend.hpp"

namespace {

struct Stepping {
  World world;
  StepBlend blend;
  EntityId entity;
  Stepping() {
    world.registerComponent<TransformComponent>({});
    entity = world.createEntity();
    world.addComponent<TransformComponent>(entity);
  }
  TransformComponent& transform() { return *world.getComponent<TransformComponent>(entity); }
  void stepTo(glm::vec2 at, StepBlend::Camera camera = {}) {
    transform().position = {at, 0.0f};
    blend.stepped(world, camera);
  }
};

}  // namespace

// Drawn between steps, a transform is part way from the step before; after the draw it's back.
TEST(StepBlend, DrawsBetweenTheLastTwoStepsThenPutsThingsBack) {
  Stepping s;
  s.stepTo({0.0f, 0.0f});
  s.stepTo({10.0f, 20.0f}, {{100.0f, 0.0f}, 2.0f});
  const StepBlend::Camera camera = s.blend.blend(s.world, {{100.0f, 0.0f}, 2.0f}, 0.25f, 4000.0f);
  EXPECT_EQ(s.transform().position, glm::vec3(2.5f, 5.0f, 0.0f));
  EXPECT_EQ(camera.center, glm::vec2(25.0f, 0.0f));
  EXPECT_FLOAT_EQ(camera.zoom, 1.25f);
  s.blend.restore(s.world);
  EXPECT_EQ(s.transform().position, glm::vec3(10.0f, 20.0f, 0.0f));
}

// A teleport or wrap is drawn where it lands, and a new scene from its first step.
TEST(StepBlend, JumpsAndNewScenesAreNotBlended) {
  Stepping s;
  s.stepTo({0.0f, 0.0f});
  s.stepTo({500.0f, 0.0f});
  s.blend.blend(s.world, {}, 0.5f, 200.0f);
  EXPECT_EQ(s.transform().position.x, 500.0f);
  s.blend.restore(s.world);

  s.stepTo({510.0f, 0.0f});
  s.blend.forget();
  s.stepTo({520.0f, 0.0f}, {{300.0f, 0.0f}, 1.0f});
  const StepBlend::Camera camera = s.blend.blend(s.world, {{300.0f, 0.0f}, 1.0f}, 0.5f, 2000.0f);
  EXPECT_EQ(s.transform().position.x, 520.0f);
  EXPECT_EQ(camera.center.x, 300.0f);
}

// A turn across the wrap (a script keeping angles in 0..2π) goes the short way.
TEST(StepBlend, TurnsTheShortWay) {
  Stepping s;
  constexpr float kPi = std::numbers::pi_v<float>;
  s.transform().rotationRad = 2.0f * kPi - 0.1f;
  s.blend.stepped(s.world, {});
  s.transform().rotationRad = 0.1f;
  s.blend.stepped(s.world, {});
  s.blend.blend(s.world, {}, 0.5f, 2000.0f);
  EXPECT_NEAR(std::remainder(s.transform().rotationRad, 2.0f * kPi), 0.0f, 1e-5f);
}

// A jump is half the view as zoomed: zoomed in, less of the world fits; zoomed out, more.
TEST(StepBlend, AJumpIsHalfTheViewAtItsZoom) {
  for (const auto& [zoom, move, blended] : {std::tuple(10.0f, 40.0f, false), std::tuple(0.1f, 120.0f, true)}) {
    Stepping s;
    s.stepTo({0.0f, 0.0f}, {{}, zoom});
    s.stepTo({move, 0.0f}, {{}, zoom});
    s.blend.blend(s.world, {{}, zoom}, 0.5f, 320.0f);  // 32 world units across at zoom 10, 3200 at 0.1
    EXPECT_EQ(s.transform().position.x, blended ? move / 2 : move) << "zoom " << zoom;
  }
  Stepping s;  // zooming out from 10 to 0.1: a teleport across what was in view still jumps
  s.stepTo({-200.0f, 0.0f}, {{}, 10.0f});
  s.stepTo({200.0f, 0.0f}, {{}, 0.1f});
  s.blend.blend(s.world, {{}, 0.1f}, 0.5f, 320.0f);
  EXPECT_EQ(s.transform().position.x, 200.0f);
}
