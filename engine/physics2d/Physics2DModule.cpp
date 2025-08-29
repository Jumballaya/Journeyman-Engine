#include "Physics2DModule.hpp"

#include <glm/glm.hpp>

#include "../core/app/Application.hpp"
#include "../core/app/Registration.hpp"
#include "TransformComponent.hpp"

REGISTER_MODULE(Physics2DModule);

void Physics2DModule::initialize(Application& app) {
  auto& ecsWorld = app.getWorld();
  // Components
  ecsWorld.registerComponent<TransformComponent, PODTransformComponent>(
      // JSON Deserialize
      [&](World& world, EntityId id, const nlohmann::json& json) {
        TransformComponent comp;

        if (json.contains("position") && json["position"].is_array()) {
          std::array<float, 3> posData = json["position"].get<std::array<float, 3>>();
          glm::vec3 position{posData[0], posData[1], posData[2]};
          comp.position = position;
        }

        if (json.contains("scale") && json["scale"].is_array()) {
          std::array<float, 2> scaleData = json["scale"].get<std::array<float, 2>>();
          glm::vec2 scale{scaleData[0], scaleData[1]};
          comp.scale = scale;
        }

        if (json.contains("rotation") && json["rotation"].is_number()) {
          float rotData = json["rotation"].get<float>();
          comp.rotationRad = rotData;
        }
        world.addComponent<TransformComponent>(id, comp);
      },
      // JSON Serialize
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<TransformComponent>(id);
        if (!comp) {
          return false;
        }

        float pos[3] = {0.0f, 0.0f, 0.0f};
        pos[0] = comp->position[0];
        pos[1] = comp->position[1];
        pos[2] = comp->position[2];
        float scale[2] = {0.0f, 0.0f};
        scale[0] = comp->scale[0];
        scale[1] = comp->scale[1];

        out["position"] = pos;
        out["scale"] = scale;
        out["rotation"] = comp->rotationRad;

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODTransformComponent)) return false;

        auto comp = world.getComponent<TransformComponent>(id);
        if (!comp) {
          return false;
        }

        PODTransformComponent pod{};
        std::memcpy(&pod, in.data(), sizeof(pod));

        comp->position[0] = pod.px;
        comp->position[1] = pod.py;
        comp->position[2] = pod.pz;
        comp->scale[0] = pod.sx;
        comp->scale[1] = pod.sy;
        comp->rotationRad = pod.rot;

        return true;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        if (out.size() < sizeof(PODTransformComponent)) return false;

        const auto* comp = world.getComponent<TransformComponent>(id);
        if (!comp) return false;

        PODTransformComponent pod{
            comp->position[0], comp->position[1], comp->position[2],
            comp->scale[0], comp->scale[1],
            comp->rotationRad};

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);
        return true;
      });
}

void Physics2DModule::shutdown(Application& app) {}