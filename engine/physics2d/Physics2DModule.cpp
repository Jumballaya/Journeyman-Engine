#include "Physics2DModule.hpp"

#include <glm/glm.hpp>

#include "../core/app/Application.hpp"
#include "../core/app/Registration.hpp"
#include "BoxColliderComponent.hpp"
#include "CollisionSystem.hpp"
#include "MovementSystem.hpp"
#include "Traits.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

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

  ecsWorld.registerComponent<VelocityComponent, PODVelocityComponent>(
      // JSON Deserialize
      [&](World& world, EntityId id, const nlohmann::json& json) {
        VelocityComponent comp;

        if (json.contains("velocity") && json["velocity"].is_array()) {
          std::array<float, 2> velData = json["velocity"].get<std::array<float, 2>>();
          glm::vec2 vel{velData[0], velData[1]};
          comp.velocity = vel;
        }

        world.addComponent<VelocityComponent>(id, comp);
      },
      // JSON Serialize
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<VelocityComponent>(id);
        if (!comp) {
          return false;
        }

        float vel[2] = {0.0f, 0.0f};
        vel[0] = comp->velocity[0];
        vel[1] = comp->velocity[1];

        out["velocity"] = vel;

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODVelocityComponent)) return false;

        auto comp = world.getComponent<VelocityComponent>(id);
        if (!comp) {
          return false;
        }

        PODVelocityComponent pod{};
        std::memcpy(&pod, in.data(), sizeof(pod));

        comp->velocity[0] = pod.vx;
        comp->velocity[1] = pod.vy;

        return true;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        if (out.size() < sizeof(PODVelocityComponent)) return false;

        const auto* comp = world.getComponent<VelocityComponent>(id);
        if (!comp) {
          return false;
        }

        PODVelocityComponent pod;
        pod.vx = comp->velocity[0];
        pod.vy = comp->velocity[1];

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);

        return true;
      });

  ecsWorld.registerComponent<BoxColliderComponent, PODBoxColliderComponent>(
      // JSON Deserialize
      [&](World& world, EntityId id, const nlohmann::json& json) {
        BoxColliderComponent comp;

        if (json.contains("size") && json["size"].is_array()) {
          std::array<float, 2> sizeData = json["size"].get<std::array<float, 2>>();
          glm::vec2 size{sizeData[0], sizeData[1]};
          comp.halfExtents = size;
        }

        if (json.contains("offset") && json["offset"].is_array()) {
          std::array<float, 2> offsetData = json["offset"].get<std::array<float, 2>>();
          glm::vec2 size{offsetData[0], offsetData[1]};
          comp.offset = size;
        }

        if (json.contains("layerMask") && json["layerMask"].is_array()) {
          uint32_t layerMask = json["layerMask"].get<uint32_t>();
          comp.layerMask = layerMask;
        }

        if (json.contains("collidesWithMask") && json["collidesWithMask"].is_array()) {
          uint32_t collidesWithMask = json["collidesWithMask"].get<uint32_t>();
          comp.collidesWithMask = collidesWithMask;
        }

        world.addComponent<BoxColliderComponent>(id, comp);
      },
      // JSON Serialize
      [&](const World& world, EntityId id, nlohmann::json& out) {
        auto comp = world.getComponent<BoxColliderComponent>(id);
        if (!comp) {
          return false;
        }

        std::array<float, 2> size = {comp->halfExtents[0] * 2.0f, comp->halfExtents[1] * 2.0f};
        std::array<float, 2> offset = {comp->offset[0], comp->offset[1]};

        out["size"] = size;
        out["offset"] = offset;
        out["layerMask"] = comp->layerMask;
        out["collidesWithMask"] = comp->collidesWithMask;

        return true;
      },
      // Deserialize POD data
      [&](World& world, EntityId id, std::span<const std::byte> in) {
        if (in.size() < sizeof(PODBoxColliderComponent)) return false;

        auto comp = world.getComponent<BoxColliderComponent>(id);
        if (!comp) {
          return false;
        }

        PODBoxColliderComponent pod{};
        std::memcpy(&pod, in.data(), sizeof(pod));

        comp->halfExtents[0] = pod.hx;
        comp->halfExtents[1] = pod.hy;
        comp->offset[0] = pod.ox;
        comp->offset[1] = pod.oy;
        comp->layerMask = pod.layerMask;
        comp->collidesWithMask = pod.collidesWithMask;

        return true;
      },
      // Serialize POD data
      [&](const World& world, EntityId id, std::span<std::byte> out, size_t& written) {
        if (out.size() < sizeof(PODBoxColliderComponent)) return false;

        const auto* comp = world.getComponent<BoxColliderComponent>(id);
        if (!comp) {
          return false;
        }

        PODBoxColliderComponent pod;
        pod.hx = comp->halfExtents[0];
        pod.hy = comp->halfExtents[1];
        pod.ox = comp->offset[0];
        pod.oy = comp->offset[1];
        pod.layerMask = comp->layerMask;
        pod.collidesWithMask = comp->collidesWithMask;

        std::memcpy(out.data(), &pod, sizeof(pod));
        written = sizeof(pod);

        return true;
      });

  ecsWorld.registerSystem<MovementSystem>();
  ecsWorld.registerSystem<CollisionSystem>(app.getEventBus(), app.getScriptManager());
}

void Physics2DModule::shutdown(Application& app) {}