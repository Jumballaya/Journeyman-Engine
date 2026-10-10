#include <gtest/gtest.h>

#include "Particles.hpp"

TEST(Particles, ABurstGoesOutOnceAndDiesAfterItsLifetime) {
  ParticleEmitterComponent e;
  e.burst = 10;
  e.lifetime = {1, 1};
  stepParticles(e, {5, 5}, 0.1f);
  EXPECT_EQ(e.particles.size(), 10u);
  EXPECT_EQ(e.burst, 0u);
  for (const auto& p : e.particles) EXPECT_EQ(p.position, glm::vec2(5, 5));  // sent out from where it is
  stepParticles(e, {50, 50}, 0.5f);
  EXPECT_EQ(e.particles.size(), 10u);
  stepParticles(e, {50, 50}, 0.6f);
  EXPECT_TRUE(e.particles.empty());
}

TEST(Particles, AStreamSendsOutItsRateHoweverFramesFall) {
  ParticleEmitterComponent a, b;
  a.rate = b.rate = 30;
  a.lifetime = b.lifetime = {10, 10};
  stepParticles(a, {}, 1.0f);
  for (int i = 0; i < 60; ++i) stepParticles(b, {}, 1.0f / 60.0f);
  EXPECT_EQ(a.particles.size(), 30u);
  EXPECT_NEAR(static_cast<float>(b.particles.size()), 30.0f, 1.0f);
  b.emitting = 0;
  stepParticles(b, {}, 1.0f);
  EXPECT_NEAR(static_cast<float>(b.particles.size()), 30.0f, 1.0f);  // stopped; those out live on
}

TEST(Particles, TheyFlyFallAndCapAtMax) {
  ParticleEmitterComponent e;
  e.burst = 1000;
  e.maxParticles = 50;
  e.speed = {10, 10};
  e.angle = 0;
  e.spread = 0;
  e.gravity = {0, -20};
  e.lifetime = {5, 5};
  stepParticles(e, {}, 0.01f);
  ASSERT_EQ(e.particles.size(), 50u);
  stepParticles(e, {}, 1.0f);
  EXPECT_NEAR(e.particles[0].position.x, 10, 1e-3f);  // right at 10/s
  EXPECT_LT(e.particles[0].position.y, -10);          // and falling
  const auto before = e.particles[0].position;
  stepParticles(e, {}, 0.0f);  // paused
  EXPECT_EQ(e.particles[0].position, before);
}

TEST(Particles, TheSameSeedSendsThemTheSameWay) {
  ParticleEmitterComponent a, b;
  a.random = b.random = 12345;
  a.burst = b.burst = 20;
  stepParticles(a, {}, 0.1f);
  stepParticles(b, {}, 0.1f);
  for (size_t i = 0; i < a.particles.size(); ++i) EXPECT_EQ(a.particles[i].velocity, b.particles[i].velocity);
  EXPECT_NE(a.particles[0].velocity, a.particles[1].velocity);
}

TEST(Particles, DrawnFadingAndShrinking) {
  ParticleEmitterComponent e;
  e.burst = 1;
  e.lifetime = {1, 1};
  e.startSize = 4;
  e.endSize = 0;
  e.startColor = {1, 0, 0, 1};
  e.endColor = {1, 0, 0, 0};
  stepParticles(e, {}, 0.01f);
  stepParticles(e, {}, 0.5f);
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  drawParticles(renderer, e, 3.0f);
  renderer.endFrame();
  ASSERT_EQ(renderer.drawnWorld().size(), 1u);
  const auto& item = renderer.drawnWorld()[0];
  EXPECT_NEAR(item.instance.color.a, 0.5f, 0.01f);
  EXPECT_NEAR(item.instance.transform[0][0], 2.0f, 0.02f);  // half way to nothing
  EXPECT_EQ(item.z, 3.0f);
  renderer.shutdown();
}
