#include <gtest/gtest.h>

#include <stb_image_write.h>

#include "../../core/tests/assets/TempDir.hpp"
#include "../../physics2d/TransformComponent.hpp"
#include "Engine.hpp"
#include "LoggerService.hpp"
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

TEST(Particles, TheyComeFromTheOffset) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<ParticleEmitterComponent>();
  const EntityId runner = world.createEntity();
  world.addComponent<TransformComponent>(runner).position = {10, 20, 0};
  auto& e = world.addComponent<ParticleEmitterComponent>(runner);
  e.offset = {0, -12};
  e.burst = 1;
  e.speed = {0, 0};
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(640, 360, RenderSettings{320, 180}, /*gpu=*/false));
  ParticleSystem().update(world, 0.01f);
  EXPECT_EQ(world.getComponent<ParticleEmitterComponent>(runner)->particles[0].position, glm::vec2(10, 8));
  renderer.shutdown();
}

TEST(Particles, AnAbsurdRateStillStopsAtMax) {
  ParticleEmitterComponent e;
  e.rate = 1e12f;
  e.maxParticles = 256;
  stepParticles(e, {}, 1.0f / 60.0f);
  EXPECT_EQ(e.particles.size(), 256u);
  EXPECT_LT(e.owed, 1.0f);
  e.rate = INFINITY;
  stepParticles(e, {}, 1.0f / 60.0f);
  EXPECT_LE(e.particles.size(), 256u);
}

// A texture that names no image is reported, as a sprite's is: not quietly a square.
// A real region, or none (squares), is fine.
TEST(Particles, AMissingTextureIsReported) {
  TempDir game;
  const unsigned char pixel[4] = {255, 255, 255, 255};
  stbi_write_png((game.path() / "fx.png").string().c_str(), 1, 1, 4, pixel, 4);
  game.writeFile("fx.atlas.json", R"({"image": "fx.png", "width": 1, "height": 1, "regions": {"smoke": [0, 0, 1, 1]}})");
  game.writeFile(".jm.json", R"({"name": "Fx", "entryScene": "main.scene.json", "scenes": ["main.scene.json"],
                                 "assets": ["fx.png", "fx.atlas.json"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [{"name": "Puff", "components":
    {"TransformComponent": {}, "ParticleEmitterComponent": {"texture": "fx.atlas.json#smkoe"}}},
    {"name": "Smoke", "components": {"TransformComponent": {}, "ParticleEmitterComponent": {"texture": "fx.atlas.json#smoke"}}},
    {"name": "Dust", "components": {"TransformComponent": {}, "ParticleEmitterComponent": {}}}]})");
  std::vector<std::string> errors;
  LoggerService::instance().setErrorListener(
      [&](LogLevel, std::string_view message, const ErrorSource&) { errors.emplace_back(message); });
  {
    EngineOptions options;
    options.dev = DevOptions{};
    options.dev.renderer = "none";
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    int emitters = 0;
    for (auto [entity, emitter] : engine.getWorld().view<ParticleEmitterComponent>()) {
      EXPECT_EQ(emitter->texture.isValid(), engine.getWorld().hasTag(entity, "Smoke"));
      ++emitters;
    }
    EXPECT_EQ(emitters, 3);
  }
  LoggerService::instance().setErrorListener(nullptr);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_NE(errors[0].find("fx.atlas.json#smkoe"), std::string::npos);
}
