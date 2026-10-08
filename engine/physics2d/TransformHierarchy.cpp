#include "TransformHierarchy.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "../core/ecs/system/SystemTraits.hpp"

namespace {

// Places every child, parents before their children, so a whole chain moves in one frame.
class TransformHierarchySystem : public System {
 public:
  void update(World& world, float) override {
    _order.clear();
    for (auto [entity, local, transform] : world.view<LocalTransformComponent, TransformComponent>()) {
      int depth = 0;
      for (EntityId up = world.parentOf(entity); up != kNoEntityId; up = world.parentOf(up)) ++depth;
      _order.emplace_back(depth, entity);
    }
    std::sort(_order.begin(), _order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [_, entity] : _order) {
      const TransformComponent* parent = world.getComponent<TransformComponent>(world.parentOf(entity));
      if (!parent) continue;  // a parent without a transform holds nowhere
      placeChild(*parent, *world.getComponent<LocalTransformComponent>(entity), *world.getComponent<TransformComponent>(entity));
    }
  }
  const char* name() const override { return "TransformHierarchySystem"; }

 private:
  std::vector<std::pair<int, EntityId>> _order;
};

}  // namespace

template <>
struct SystemTraits<TransformHierarchySystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = TypeList<LocalTransformComponent>;
  using Writes = TypeList<TransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

void placeChild(const TransformComponent& parent, const LocalTransformComponent& local, TransformComponent& child) {
  const float c = std::cos(parent.rotationRad), s = std::sin(parent.rotationRad);
  child.position = {parent.position.x + c * local.position.x - s * local.position.y,
                    parent.position.y + s * local.position.x + c * local.position.y, parent.position.z + local.position.z};
  child.rotationRad = parent.rotationRad + local.rotationRad;
}

LocalTransformComponent localTo(const TransformComponent& parent, const TransformComponent& child) {
  const glm::vec3 d = child.position - parent.position;
  const float c = std::cos(parent.rotationRad), s = std::sin(parent.rotationRad);
  LocalTransformComponent local;
  local.position = {c * d.x + s * d.y, -s * d.x + c * d.y, d.z};
  local.rotationRad = child.rotationRad - parent.rotationRad;
  return local;
}

void registerLocalTransform(World& world) {
  world.registerComponent<LocalTransformComponent>({
      .scriptFields = {
          scriptField<LocalTransformComponent>("x", [](LocalTransformComponent& c) -> float& { return c.position.x; }),
          scriptField<LocalTransformComponent>("y", [](LocalTransformComponent& c) -> float& { return c.position.y; }),
          scriptField<LocalTransformComponent>("z", [](LocalTransformComponent& c) -> float& { return c.position.z; }),
          scriptField<LocalTransformComponent>("rotation", [](LocalTransformComponent& c) -> float& { return c.rotationRad; }),
      },
  });
}

void installTransformHierarchy(World& world) {
  registerLocalTransform(world);
  world.onParentChanged([&world](EntityId child, EntityId parentId, World::Attach how) {
    const TransformComponent* own = world.getComponent<TransformComponent>(child);
    const TransformComponent* parentTransform = world.getComponent<TransformComponent>(parentId);
    if (!own) return;  // nothing to place
    if (!parentTransform) {  // detached (or under nothing placeable): it stays where it is
      world.removeComponent<LocalTransformComponent>(child);
      return;
    }
    // Copies: adding a component moves rows, and may move the parent's too.
    const TransformComponent parent = *parentTransform;
    LocalTransformComponent local;
    if (how == World::Attach::AsAuthored) {
      local.position = own->position;
      local.rotationRad = own->rotationRad;
    } else {
      local = localTo(parent, *own);
    }
    if (auto* existing = world.getComponent<LocalTransformComponent>(child)) *existing = local;
    else world.addComponent<LocalTransformComponent>(child, local);
    placeChild(parent, local, *world.getComponent<TransformComponent>(child));
  });
  world.registerSystem<TransformHierarchySystem>();
}
