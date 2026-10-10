#include <gtest/gtest.h>

#include "../../physics2d/TransformComponent.hpp"
#include "../Lights.hpp"
#include "../Renderer2DSystem.hpp"

namespace {
void registerLights(World& world) {
  world.registerComponent<TransformComponent>({});
  world.registerComponent<PointLightComponent>({});
  world.registerComponent<AmbientLightComponent>({});
}
}  // namespace

TEST(Lights, AWorldWithoutLightsIsUnlit) {
  World world;
  registerLights(world);
  world.addComponent<TransformComponent>(world.createEntity());
  EXPECT_FALSE(Renderer2DSystem::gatherLights(world).on);  // drawn exactly as before lighting existed
}

TEST(Lights, GathersAmbientAndPointLightsWithTheirEnergy) {
  World world;
  registerLights(world);
  world.addComponent<AmbientLightComponent>(world.createEntity()) = {.color = glm::vec3(0.5f), .energy = 0.5f};
  const EntityId lamp = world.createEntity();
  world.addComponent<TransformComponent>(lamp).position = {100.0f, 50.0f, 0.0f};
  auto& light = world.addComponent<PointLightComponent>(lamp);
  light.color = {1.0f, 0.5f, 0.0f};
  light.energy = 2.0f;
  light.offset = {0.0f, 10.0f};

  const Lighting lighting = Renderer2DSystem::gatherLights(world);
  EXPECT_TRUE(lighting.on);
  EXPECT_EQ(lighting.ambient, glm::vec3(0.25f));
  ASSERT_EQ(lighting.lights.size(), 1u);
  EXPECT_EQ(lighting.lights[0].position, glm::vec2(100.0f, 60.0f));
  EXPECT_EQ(lighting.lights[0].color, glm::vec3(2.0f, 1.0f, 0.0f));

  World justLamps;  // point lights alone: the ambient stays full light, so they only brighten
  registerLights(justLamps);
  const EntityId only = justLamps.createEntity();
  justLamps.addComponent<TransformComponent>(only);
  justLamps.addComponent<PointLightComponent>(only);
  EXPECT_TRUE(Renderer2DSystem::gatherLights(justLamps).on);
  EXPECT_EQ(Renderer2DSystem::gatherLights(justLamps).ambient, glm::vec3(1.0f));
}
