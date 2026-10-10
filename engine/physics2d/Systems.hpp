#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "Blocking.hpp"
#include "Colliders.hpp"
#include "LifetimeComponent.hpp"
#include "ScrollWrapComponent.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

// The arcade physics: motion, lifetimes, scroll wrapping and overlap reports.
// Physics2DModule registers them; tests run them on a bare World.

// A frame's step for simulation: at most 1/20 s (a hitch slows the game rather
// than teleporting through it), and 0 for a nonsense dt.
float simulationStep(float dt);

// velocity += acceleration * dt, then position += velocity * dt: freely, or
// for a move/walk motion through moveBlocked/walkBlocked, which zero the
// velocity along a side that's blocked (landing stops a fall).
// Move/walk bodies go carriers first (a rider after what it stands on). With a
// `frame`, they share it with the frame's other moves (scripts').
class MovementSystem : public System {
 public:
  explicit MovementSystem(MoveFrame* frame = nullptr) : _frame(frame) {}
  void update(World& world, float dt) override;
  const char* name() const override { return "MovementSystem"; }

 private:
  MoveFrame* _frame;
  std::vector<std::pair<EntityId, glm::vec2>> _was;  // where each body started the step
  std::vector<EntityId> _blocked;                        // move/walk bodies, carriers first
  std::vector<std::pair<EntityId, glm::vec2>> _stopped;  // where each of those stopped (later pushes may move it off)
};

// Counts lifetimes down with movement's step; destroys (deferred) at zero.
class LifetimeSystem : public System {
 public:
  void update(World& world, float dt) override;
  const char* name() const override { return "LifetimeSystem"; }
};

// Wraps y into [minY, maxY] (scrolling backdrops).
class ScrollWrapSystem : public System {
 public:
  void update(World& world, float) override;
  const char* name() const override { return "ScrollWrapSystem"; }
};

// Reports each overlapping pair of colliders, once a frame, when either's
// collisionLayer meets the other's collisionMask. A body counts as moving once it
// has a VelocityComponent or has ever changed position; two that never move
// never collide. Bodies are tested along the way they went this frame (moved,
// walked or carried; else straight by their velocity's travel), so a fast one
// can't pass through a thin one between frames. Boxes and circles
// both collide; a pair of entities is reported once, in forEachCollider's
// order, the earlier one first.
class CollisionSystem : public System {
 public:
  using Report = std::function<void(EntityId a, EntityId b)>;
  // `moves`: the way bodies went this frame, swept along it (else straight, by their velocity's travel).
  explicit CollisionSystem(Report report, const MoveFrame* moves = nullptr) : _report(std::move(report)), _moves(moves) {}

  void update(World& world, float dt) override;
  const char* name() const override { return "CollisionSystem"; }

 private:
  struct Body {
    std::optional<glm::vec2> box, circle;  // where each of its colliders was
    bool moves = false;
  };
  struct Proxy {
    Collider collider;
    uint32_t way, turns;  // its way this frame: _ways[way, way + turns), offsets from its center
    glm::vec2 min, max;   // bounds over that way
    bool moves;
  };
  void addProxy(World& world, const Collider& collider);
  std::span<const glm::vec2> wayOf(const Proxy& p) const { return {_ways.data() + p.way, p.turns}; }

  Report _report;
  const MoveFrame* _moves;
  std::vector<glm::vec2> _ways;
  std::vector<Proxy> _proxies;  // this frame's colliders, in world order
  std::unordered_map<EntityId, Body> _bodies, _nextBodies;  // last frame's, and this one's being made
  // The sweep's scratch, kept to save allocating every frame.
  std::vector<uint32_t> _byLeft, _active;
  std::vector<std::pair<uint32_t, uint32_t>> _pairs;
  std::vector<EntityId> _twoShaped;                      // this frame's entities with a box and a circle
  std::vector<std::pair<EntityId, EntityId>> _reported;  // pairs reported with one of them
};

struct Physics2D_Moved {};  // provided by MovementSystem

template <>
struct SystemTraits<MovementSystem> {
  using DependsOn = EmptyList;
  using Provides = TypeList<Physics2D_Moved>;
  using Reads = TypeList<VelocityComponent, TransformComponent>;
  using Writes = TypeList<TransformComponent, VelocityComponent>;  // acceleration changes velocity
  static constexpr SystemStage stage = SystemStage::Physics;
};

template <>
struct SystemTraits<CollisionSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = EmptyList;
  using Reads = TypeList<TransformComponent, BoxColliderComponent, CircleColliderComponent, VelocityComponent>;
  using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};

template <>
struct SystemTraits<LifetimeSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = TypeList<LifetimeComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

template <>
struct SystemTraits<ScrollWrapSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = EmptyList;
  using Reads = TypeList<ScrollWrapComponent>;
  using Writes = TypeList<TransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};
