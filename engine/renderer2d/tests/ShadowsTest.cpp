#include <gtest/gtest.h>

#include <numbers>

#include "../../physics2d/Colliders.hpp"
#include "../../physics2d/Terrain.hpp"
#include "../Renderer2DSystem.hpp"
#include "../Shadows.hpp"

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

void addLamp(World& world, bool shadows) {
  world.registerComponent<TransformComponent>({});
  world.registerComponent<PointLightComponent>({});
  world.registerComponent<LightOccluderComponent>({});
  world.registerComponent<BoxColliderComponent>({});
  world.registerComponent<CircleColliderComponent>({});
  world.registerComponent<GroundComponent>({});
  const EntityId lamp = world.createEntity();
  world.addComponent<TransformComponent>(lamp);
  world.addComponent<PointLightComponent>(lamp).shadows = shadows;
}

EntityId addAt(World& world, glm::vec2 at) {
  const EntityId e = world.createEntity();
  world.addComponent<TransformComponent>(e).position = {at, 0.0f};
  return e;
}

// The CPU twin of the shadow pass: a light's row, each column its nearest caster's distance.
std::vector<float> shadowRow(const std::vector<ShadowCaster>& casters, float row) {
  std::vector<float> out(kShadowAngles, INFINITY);
  for (const ShadowCaster& c : casters) {
    if (c.row != row) continue;
    for (int col = 0; col < kShadowAngles; ++col) {
      const float angle = shadowColumnAngle(col);
      if (angle < c.span.x || angle > c.span.y) continue;
      if (auto d = shadowDistance(c.light, angle, {c.segment.x, c.segment.y}, {c.segment.z, c.segment.w}))
        out[col] = std::min(out[col], *d);
    }
  }
  return out;
}

// As the sprite shader's hard shadows: the depth between the two columns nearest `p`.
float hardDepth(const std::vector<float>& row, glm::vec2 light, glm::vec2 p) {
  const glm::vec2 to = p - light;
  const float column = (std::atan2(to.y, to.x) / (2.0f * kPi) + 0.5f) * kShadowAngles - 0.5f;
  const int c = static_cast<int>(std::floor(column));
  return glm::mix(row[c & (kShadowAngles - 1)], row[(c + 1) & (kShadowAngles - 1)], column - c);
}

int columnOf(float angle) { return static_cast<int>((angle + kPi) / (2.0f * kPi) * kShadowAngles); }

}  // namespace

TEST(Shadows, OccludersAreGatheredOnlyForShadowCastingLights) {
  for (const bool shadows : {false, true}) {
    World world;
    addLamp(world, shadows);
    const EntityId pillar = addAt(world, {50.0f, 0.0f});
    world.addComponent<BoxColliderComponent>(pillar).halfExtents = {5.0f, 20.0f};
    world.addComponent<LightOccluderComponent>(pillar);
    const Lighting lighting = Renderer2DSystem::gatherLights(world);
    EXPECT_EQ(lighting.occluders.size(), shadows ? 1u : 0u);  // no shadows: nothing extra gathered
  }
}

TEST(Shadows, OccludersComeFromCollidersAndOccludingGround) {
  World world;
  addLamp(world, true);
  const EntityId pillar = addAt(world, {50.0f, 0.0f});
  world.addComponent<BoxColliderComponent>(pillar).halfExtents = {5.0f, 20.0f};
  world.addComponent<LightOccluderComponent>(pillar);
  const EntityId barrel = addAt(world, {-50.0f, 0.0f});
  world.addComponent<CircleColliderComponent>(barrel).radius = 10.0f;
  world.addComponent<LightOccluderComponent>(barrel);
  // a collider without LightOccluderComponent casts nothing
  world.addComponent<BoxColliderComponent>(addAt(world, {0.0f, 50.0f})).halfExtents = {5.0f, 5.0f};
  const EntityId ground = addAt(world, {0.0f, -40.0f});
  auto& terrain = world.addComponent<GroundComponent>(ground);
  terrain.chains.emplace_back(std::vector<glm::vec2>{{0, 0}, {0, 10}, {10, 10}, {10, 0}}, true, false, true);  // clockwise
  terrain.chains.emplace_back(std::vector<glm::vec2>{{0, 0}, {10, 0}}, false, false, false);

  const auto occluders = Renderer2DSystem::gatherLights(world).occluders;
  ASSERT_EQ(occluders.size(), 3u);
  EXPECT_EQ(occluders[0].points, (std::vector<glm::vec2>{{45, -20}, {55, -20}, {55, 20}, {45, 20}}));
  EXPECT_EQ(occluders[1].points.size(), 16u);  // the circle as a polygon
  EXPECT_NEAR(glm::distance(occluders[1].points[4], glm::vec2(-50, 0)), 10.0f, 1e-4f);
  // Ground in world space, turned counterclockwise.
  EXPECT_EQ(occluders[2].points, (std::vector<glm::vec2>{{10, -40}, {10, -30}, {0, -30}, {0, -40}}));
  EXPECT_TRUE(occluders[2].closed);
}

TEST(Shadows, NoShadowCastingLightMeansNoShadowPass) {
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 100.0f, 1.0f, 64.0f, std::nullopt};
  const std::vector<const Lighting::Light*> lights{&lamp};
  const std::vector<Lighting::Occluder> wall{{{{10, -10}, {10, 10}}, false}};
  EXPECT_TRUE(shadowCasters(lights, wall).empty());
  lamp.shadowSoftness = 1.0f;
  EXPECT_FALSE(shadowCasters(lights, wall).empty());
  lamp.radius = 5.0f;  // out of its reach
  EXPECT_TRUE(shadowCasters(lights, wall).empty());
}

// A box right of the light: its row holds the box's far side (its inside stays
// lit) toward it, nothing behind the light.
TEST(Shadows, ARowHoldsTheDistanceToTheFarSideOfABox) {
  Lighting::Light dark{{0, 0}, glm::vec3(1), 100.0f, 1.0f, 64.0f, std::nullopt};
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 100.0f, 1.0f, 64.0f, 1.0f};
  const std::vector<const Lighting::Light*> lights{&dark, &lamp};
  const std::vector<Lighting::Occluder> box{{{{10, -5}, {20, -5}, {20, 5}, {10, 5}}, true}};
  const auto casters = shadowCasters(lights, box);
  for (const ShadowCaster& c : casters) EXPECT_EQ(c.row, 1.0f);  // the second light's row

  const std::vector<float> row = shadowRow(casters, 1.0f);
  EXPECT_NEAR(row[columnOf(0.0f)], 20.0f, 0.01f);
  EXPECT_NEAR(row[columnOf(std::atan2(4.0f, 20.0f))], 20.0f / std::cos(std::atan2(4.0f, 20.0f)), 0.1f);
  EXPECT_EQ(row[columnOf(kPi / 2)], INFINITY);
  EXPECT_EQ(row[columnOf(kPi * 0.99f)], INFINITY);
}

// A wall left of the light spans the ±π seam: both ends of the row hold it.
TEST(Shadows, ASpanAcrossTheSeamWrapsToBothEndsOfTheRow) {
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 100.0f, 1.0f, 64.0f, 0.0f};
  const std::vector<const Lighting::Light*> lights{&lamp};
  const std::vector<Lighting::Occluder> wall{{{{-30, -10}, {-30, 10}}, false}};
  const std::vector<float> row = shadowRow(shadowCasters(lights, wall), 0.0f);
  EXPECT_NEAR(row[0], 30.0f, 0.01f);
  EXPECT_NEAR(row[kShadowAngles - 1], 30.0f, 0.01f);
  EXPECT_EQ(row[kShadowAngles / 2], INFINITY);
  EXPECT_FALSE(shadowDistance({0, 0}, 0.0f, {-30, -10}, {-30, 10}));  // behind the light
}

// Review: a padded texel or a ray along the segment's line takes its nearer end, not the line.
TEST(Shadows, ARayMissingASegmentTakesItsNearerEnd) {
  EXPECT_NEAR(*shadowDistance({0, 0}, 0.001f, {10, 0}, {20, 0}), 10.0f, 1e-4f);  // along its line
  EXPECT_NEAR(*shadowDistance({0, 0}, 0.5f, {10, -5}, {10, 1}), std::sqrt(101.0f), 1e-4f);  // past its end
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 100.0f, 1.0f, 64.0f, 0.0f};
  const std::vector<const Lighting::Light*> lights{&lamp};
  const std::vector<Lighting::Occluder> line{{{{10, 0}, {20, 0}}, false}};
  const std::vector<float> row = shadowRow(shadowCasters(lights, line), 0.0f);
  for (const int col : {columnOf(0.0f) - 1, columnOf(0.0f), columnOf(0.0f) + 1})
    EXPECT_TRUE(row[col] == INFINITY || row[col] >= 10.0f);  // nothing in front of it
}

TEST(Shadows, ZeroLengthSegmentsCastNothing) {
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 100.0f, 1.0f, 64.0f, 0.0f};
  const std::vector<const Lighting::Light*> lights{&lamp};
  const std::vector<Lighting::Occluder> dots{{{{10, 0}, {10, 0}}, false}, {{{5, 5}, {5, 5}, {5, 5}}, true}};
  EXPECT_TRUE(shadowCasters(lights, dots).empty());
}

TEST(Shadows, GroundFarFromTheOriginStillTurnsCounterclockwise) {
  World world;
  addLamp(world, true);
  const EntityId ground = addAt(world, {100000.0f, 100000.0f});
  world.addComponent<GroundComponent>(ground).chains.emplace_back(
      std::vector<glm::vec2>{{0, 0}, {0, 10}, {10, 10}, {10, 0}}, true, false, true);  // clockwise
  const auto occluders = Renderer2DSystem::gatherLights(world).occluders;
  ASSERT_EQ(occluders.size(), 1u);
  EXPECT_EQ(occluders[0].points.front(), glm::vec2(100010.0f, 100000.0f));
}

// Review: a closed shape's side seen edge-on (the light on its line) would shadow the shape's own inside.
TEST(Shadows, ASideSeenEdgeOnCastsNothing) {
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 128.0f, 1.0f, 64.0f, 0.0f};
  const std::vector<const Lighting::Light*> lights{&lamp};
  const std::vector<Lighting::Occluder> box{{{{10, 0}, {100, 0}, {100, 1}, {10, 1}}, true}};
  for (const ShadowCaster& c : shadowCasters(lights, box)) EXPECT_NE(c.segment, glm::vec4(10, 0, 100, 0));
}

// Review: a side's pad into the next far side took its nearer end, darkening the box's own inside by its far corners.
TEST(Shadows, ABoxsInsideStaysLitUpToItsFarCorners) {
  Lighting::Light lamp{{-80, 0}, glm::vec3(1), 600.0f, 0.0f, 64.0f, 0.0f};
  const std::vector<const Lighting::Light*> lights{&lamp};
  const std::vector<Lighting::Occluder> box{{{{-30, -20}, {-10, -20}, {-10, 20}, {-30, 20}}, true}};
  const std::vector<float> row = shadowRow(shadowCasters(lights, box), 0.0f);
  for (float x = -29.5f; x < -10.0f; x += 1.0f) {
    for (float y = -19.5f; y < 20.0f; y += 1.0f) {
      const glm::vec2 p(x, y);
      EXPECT_LE(glm::distance(p, lamp.position), hardDepth(row, lamp.position, p) + 1.0f) << x << ", " << y;
    }
  }
}

// Self-review: thin features narrower than a texel keep shadowing what's behind them: a grazing wall,
// a grazing triangle whose next side is out of reach, and a notch whose tip points at the light.
TEST(Shadows, ThinFeaturesStillShadowWhatsBehindThem) {
  Lighting::Light lamp{{0, 0}, glm::vec3(1), 600.0f, 0.0f, 64.0f, 0.0f};
  auto behind = [&](std::vector<Lighting::Occluder> shape, glm::vec2 p) {
    const std::vector<const Lighting::Light*> lights{&lamp};
    const std::vector<float> row = shadowRow(shadowCasters(lights, shape), 0.0f);
    return hardDepth(row, lamp.position, p) + 1.0f < glm::distance(p, lamp.position);
  };
  EXPECT_TRUE(behind({{{{10, -0.05f}, {100, 0.25f}}, false}}, {40, 0}));  // it crosses y = 0 at x = 25
  EXPECT_TRUE(behind({{{{10, -10}, {100, -1}, {20, 0}, {100, 1}, {10, 10}}, true}}, {25, 0}));  // the tip at 20
  lamp.radius = 50.0f;
  EXPECT_TRUE(behind({{{{10, -0.05f}, {100, 0.25f}, {101, 1}}, true}}, {40, 0}));
}
