#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

// 2D lighting. Without either component in the world, nothing is lit: sprites
// draw as they always have. With them, world sprites (not the UI) are lit:
// their color times the ambient light plus each point light that reaches them.

// A light that shines from its entity (Godot's PointLight2D, Unity's point Light 2D).
struct PointLightComponent : public Component<PointLightComponent> {
  COMPONENT_NAME("PointLightComponent");
  glm::vec3 color{1.0f};
  float energy = 1.0f;     // brightness: 1 lights a pixel by its color, more saturates
  float radius = 128.0f;   // world units to where it fades out
  float falloff = 2.0f;    // how it fades: 1 linear, higher drops off sooner
  glm::vec2 offset{0.0f};  // from the entity's position
  float height = 64.0f;    // above the sprites: lower grazes normal-mapped ones from the side
  bool shadows = false;    // occluders (LightOccluderComponent, occluding terrain) cast its shadows
  float shadowSoftness = 1.0f;  // 0: hard edges; higher blurs them wider
};

// Casts shadows from shadow-casting lights, shaped like the entity's box or
// circle collider (Godot's LightOccluder2D, Unity's ShadowCaster2D).
struct LightOccluderComponent : public Component<LightOccluderComponent> {
  COMPONENT_NAME("LightOccluderComponent");
};

// The light everything gets before point lights (Godot's CanvasModulate,
// Unity's Global Light 2D). One per world; dark makes lights matter.
struct AmbientLightComponent : public Component<AmbientLightComponent> {
  COMPONENT_NAME("AmbientLightComponent");
  glm::vec3 color{1.0f};
  float energy = 1.0f;
};

// What the renderer lights a frame with.
struct Lighting {
  struct Light {
    glm::vec2 position;
    glm::vec3 color;  // times energy
    float radius;
    float falloff;
    float height;
    std::optional<float> shadowSoftness;  // set: it casts shadows
  };
  // An outline that blocks shadow-casting lights, world space. Closed ones
  // run counterclockwise: their insides stay lit, the shadow starts behind.
  struct Occluder {
    std::vector<glm::vec2> points;
    bool closed;
  };
  bool on = false;  // false: unlit (no light components)
  glm::vec3 ambient{1.0f};
  std::vector<Light> lights;
  std::vector<Occluder> occluders;  // only when a light casts shadows
};
