#include <gtest/gtest.h>
#include <glm/gtc/constants.hpp>

#include "../SpriteComponent.hpp"

TEST(SpriteShadow, DisabledByDefaultAndDisappearsWithOwnerOpacity) {
  SpriteComponent sprite;
  TransformComponent transform;
  EXPECT_FALSE(sprite.shadow.instance(transform, 1.0f, sprite.texRect));
  sprite.shadow.color.a = 0.3f;
  EXPECT_TRUE(sprite.shadow.instance(transform, 1.0f, sprite.texRect));
  EXPECT_FALSE(sprite.shadow.instance(transform, 0.0f, sprite.texRect));
  sprite.shadow.scale = 0.0f;
  EXPECT_FALSE(sprite.shadow.instance(transform, 1.0f, sprite.texRect));
}

TEST(SpriteShadow, FollowsTransformWithWorldOffsetAndRelativeScale) {
  SpriteComponent sprite;
  sprite.shadow.color = {0.02f, 0.05f, 0.12f, 0.3f};
  sprite.shadow.offset = {16.0f, -24.0f};
  sprite.shadow.scale = 0.75f;
  TransformComponent owner;
  owner.position = {10.0f, 20.0f, 7.0f};
  owner.scale = {32.0f, 16.0f};
  owner.rotationRad = glm::half_pi<float>();
  auto shadow = sprite.shadow.instance(owner, 0.5f, sprite.texRect);
  ASSERT_TRUE(shadow);
  EXPECT_FLOAT_EQ(shadow->transform[3].x, 26.0f);
  EXPECT_FLOAT_EQ(shadow->transform[3].y, -4.0f);
  EXPECT_FLOAT_EQ(shadow->transform[3].z, 6.99f);
  EXPECT_NEAR(shadow->transform[0].x, 0.0f, 0.00001f);
  EXPECT_FLOAT_EQ(shadow->transform[0].y, 24.0f);
  EXPECT_FLOAT_EQ(shadow->transform[1].x, -12.0f);
  EXPECT_FLOAT_EQ(shadow->color.a, 0.15f);
  EXPECT_EQ(owner.position, glm::vec3(10.0f, 20.0f, 7.0f));
  owner.position.z = 9.0f;
  EXPECT_FLOAT_EQ(sprite.shadow.instance(owner, 1, sprite.texRect)->transform[3].z, 8.99f);
}

TEST(SpriteShadow, ExplicitLayerAndCurrentAnimationRegionAreUsedEveryFrame) {
  SpriteComponent sprite;
  sprite.shadow.color.a = 0.3f;
  sprite.shadow.layer = 2.0f;
  TransformComponent owner;
  owner.position.z = 7.0f;
  sprite.texRect = {0.5f, 0.25f, 0.25f, 0.25f};
  auto shadow = sprite.shadow.instance(owner, 1, sprite.texRect);
  ASSERT_TRUE(shadow);
  EXPECT_EQ(shadow->texRect, sprite.texRect);
  EXPECT_FLOAT_EQ(shadow->transform[3].z, 2.0f);
  sprite.texRect.x = 0.75f;
  EXPECT_EQ(sprite.shadow.instance(owner, 1, sprite.texRect)->texRect, sprite.texRect);
}
