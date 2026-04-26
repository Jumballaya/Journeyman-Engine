#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <span>

#include "World.hpp"
#include "component/Component.hpp"

namespace {

struct VtableTrackedA : Component<VtableTrackedA> {
  COMPONENT_NAME("VtableTrackedA");

  static inline std::atomic<int> defaultCtorCount{0};
  static inline std::atomic<int> moveCtorCount{0};
  static inline std::atomic<int> copyCtorCount{0};
  static inline std::atomic<int> dtorCount{0};

  int payload = 42;

  VtableTrackedA() { defaultCtorCount.fetch_add(1); }
  VtableTrackedA(const VtableTrackedA& other) : payload(other.payload) {
    copyCtorCount.fetch_add(1);
  }
  VtableTrackedA(VtableTrackedA&& other) noexcept : payload(other.payload) {
    moveCtorCount.fetch_add(1);
  }
  ~VtableTrackedA() { dtorCount.fetch_add(1); }

  static void resetCounts() {
    defaultCtorCount.store(0);
    moveCtorCount.store(0);
    copyCtorCount.store(0);
    dtorCount.store(0);
  }
};

struct VtableTrackedB : Component<VtableTrackedB> {
  COMPONENT_NAME("VtableTrackedB");
  int v = 0;
};

struct VtableTrackedC : Component<VtableTrackedC> {
  COMPONENT_NAME("VtableTrackedC");
  int v = 0;
};

struct VtablePod {
  int payload = 0;
};

template <typename T>
void registerNoop(World& world) {
  world.registerComponent<T, VtablePod>(
      [](World&, EntityId, const nlohmann::json&) {},
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return false; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return false; });
}

}  // namespace

// Each vtable op fires exactly the matching constructor/destructor on T.
TEST(ComponentVtable, OperationsInvokeMatchingCtorDtor) {
  World world;
  VtableTrackedA::resetCounts();
  registerNoop<VtableTrackedA>(world);

  const auto* info = world.getComponentRegistry().getInfo(VtableTrackedA::typeId());
  ASSERT_NE(info, nullptr);
  ASSERT_NE(info->defaultConstruct, nullptr);
  ASSERT_NE(info->destruct, nullptr);
  ASSERT_NE(info->moveConstruct, nullptr);
  ASSERT_NE(info->copyConstruct, nullptr);
  EXPECT_EQ(info->size, sizeof(VtableTrackedA));
  EXPECT_EQ(info->alignment, alignof(VtableTrackedA));

  alignas(VtableTrackedA) std::byte bufA[sizeof(VtableTrackedA)];
  alignas(VtableTrackedA) std::byte bufB[sizeof(VtableTrackedA)];
  alignas(VtableTrackedA) std::byte bufC[sizeof(VtableTrackedA)];

  info->defaultConstruct(bufA);
  EXPECT_EQ(VtableTrackedA::defaultCtorCount.load(), 1);
  EXPECT_EQ(reinterpret_cast<VtableTrackedA*>(bufA)->payload, 42);

  reinterpret_cast<VtableTrackedA*>(bufA)->payload = 7;

  info->copyConstruct(bufB, bufA);
  EXPECT_EQ(VtableTrackedA::copyCtorCount.load(), 1);
  EXPECT_EQ(reinterpret_cast<VtableTrackedA*>(bufB)->payload, 7);

  info->moveConstruct(bufC, bufA);
  EXPECT_EQ(VtableTrackedA::moveCtorCount.load(), 1);
  EXPECT_EQ(reinterpret_cast<VtableTrackedA*>(bufC)->payload, 7);

  info->destruct(bufA);
  info->destruct(bufB);
  info->destruct(bufC);
  EXPECT_EQ(VtableTrackedA::dtorCount.load(), 3);
}

// bitIndex is assigned sequentially starting at 0 in registration order, with
// each registered component getting a distinct index.
TEST(ComponentVtable, BitIndexIsSequentialAcrossRegistrations) {
  World world;
  registerNoop<VtableTrackedA>(world);
  registerNoop<VtableTrackedB>(world);
  registerNoop<VtableTrackedC>(world);

  const auto& reg = world.getComponentRegistry();
  const auto* a = reg.getInfo(VtableTrackedA::typeId());
  const auto* b = reg.getInfo(VtableTrackedB::typeId());
  const auto* c = reg.getInfo(VtableTrackedC::typeId());
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);

  EXPECT_EQ(a->bitIndex, 0u);
  EXPECT_EQ(b->bitIndex, 1u);
  EXPECT_EQ(c->bitIndex, 2u);
}

// Re-registering the same component does not consume another bitIndex slot.
TEST(ComponentVtable, ReregisteringDoesNotAdvanceBitIndex) {
  World world;
  registerNoop<VtableTrackedA>(world);
  registerNoop<VtableTrackedA>(world);
  registerNoop<VtableTrackedB>(world);

  const auto& reg = world.getComponentRegistry();
  const auto* a = reg.getInfo(VtableTrackedA::typeId());
  const auto* b = reg.getInfo(VtableTrackedB::typeId());
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  EXPECT_EQ(a->bitIndex, 0u);
  EXPECT_EQ(b->bitIndex, 1u);
}

// onDestroy hook tests (D.2). The hook fires when an entity is destroyed via
// World::destroyEntity; it must NOT fire on archetype migrations triggered by
// addComponent / removeComponent.

namespace {

struct DestroyHookTracked : Component<DestroyHookTracked> {
  COMPONENT_NAME("DestroyHookTracked");
  static inline std::atomic<int> destroyCount{0};
  int marker = 42;

  static void resetCounts() { destroyCount.store(0); }
  static void onDestroyHook(void* p) {
    auto* self = static_cast<DestroyHookTracked*>(p);
    if (self->marker == 42) {
      destroyCount.fetch_add(1);
    } else {
      // Distinct counter sentinel: marker mismatch flags pointer-validity
      // failure to whichever test consumes it.
      destroyCount.fetch_add(1000);
    }
  }
};

struct DestroyHookTrackedB : Component<DestroyHookTrackedB> {
  COMPONENT_NAME("DestroyHookTrackedB");
  static inline std::atomic<int> destroyCount{0};
  int marker = 7;

  static void resetCounts() { destroyCount.store(0); }
  static void onDestroyHook(void* p) {
    auto* self = static_cast<DestroyHookTrackedB*>(p);
    if (self->marker == 7) destroyCount.fetch_add(1);
  }
};

struct PassiveA : Component<PassiveA> {
  COMPONENT_NAME("PassiveA");
  int v = 0;
};

struct PassiveB : Component<PassiveB> {
  COMPONENT_NAME("PassiveB");
  int v = 0;
};

template <typename T>
void registerWithDestroyHook(World& world, void (*hook)(void*)) {
  world.registerComponent<T, VtablePod>(
      [](World&, EntityId, const nlohmann::json&) {},
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return false; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return false; },
      hook);
}

}  // namespace

// Hook fires exactly once when destroyEntity is called on an entity holding
// the tracked component.
TEST(ComponentVtable, OnDestroyFiresOnEntityDestroy) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  ASSERT_EQ(DestroyHookTracked::destroyCount.load(), 0);

  world.destroyEntity(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 1);
}

// An entity with one tracked component plus N passive components fires the
// tracked hook exactly once — the hook is per-component, but the entity only
// carries one hooked component.
TEST(ComponentVtable, OnDestroyFiresOncePerEntityRegardlessOfComponentCount) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);
  registerNoop<PassiveA>(world);
  registerNoop<PassiveB>(world);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  world.addComponent<PassiveA>(id);
  world.addComponent<PassiveB>(id);

  world.destroyEntity(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 1);
}

// Adding a second component triggers an archetype migration (which calls
// destroyRow internally on the source). The hook must NOT fire — the entity
// is still alive and the component data has been moved to the new archetype.
TEST(ComponentVtable, OnDestroyDoesNotFireOnArchetypeTransitionViaAdd) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);
  registerNoop<PassiveA>(world);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  ASSERT_EQ(DestroyHookTracked::destroyCount.load(), 0);

  world.addComponent<PassiveA>(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 0);
}

// removeComponent also triggers a migration and must not fire the hook.
TEST(ComponentVtable, OnDestroyDoesNotFireOnArchetypeTransitionViaRemove) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);
  registerNoop<PassiveA>(world);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  world.addComponent<PassiveA>(id);
  ASSERT_EQ(DestroyHookTracked::destroyCount.load(), 0);

  world.removeComponent<PassiveA>(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 0);
}

// Multiple migrations in a row stay hook-free; the hook only fires when the
// entity itself is finally destroyed.
TEST(ComponentVtable, OnDestroyFiresAfterSeveralMigrations) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);
  registerNoop<PassiveA>(world);
  registerNoop<PassiveB>(world);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  world.addComponent<PassiveA>(id);
  world.addComponent<PassiveB>(id);
  world.removeComponent<PassiveA>(id);
  ASSERT_EQ(DestroyHookTracked::destroyCount.load(), 0);

  world.destroyEntity(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 1);
}

// Two distinct hooked component types on the same entity each fire their
// own hook once.
TEST(ComponentVtable, OnDestroyFiresForEveryComponentWithAHook) {
  World world;
  DestroyHookTracked::resetCounts();
  DestroyHookTrackedB::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);
  registerWithDestroyHook<DestroyHookTrackedB>(world, &DestroyHookTrackedB::onDestroyHook);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  world.addComponent<DestroyHookTrackedB>(id);

  world.destroyEntity(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 1);
  EXPECT_EQ(DestroyHookTrackedB::destroyCount.load(), 1);
}

// A component registered without a hook is destroyed cleanly — destroyEntity
// is a no-op for the hook and does not crash.
TEST(ComponentVtable, OnDestroyNullIsNoOp) {
  World world;
  registerNoop<PassiveA>(world);

  EntityId id = world.createEntity();
  world.addComponent<PassiveA>(id);
  EXPECT_NO_THROW(world.destroyEntity(id));
  EXPECT_FALSE(world.isAlive(id));
}

// The hook receives a pointer to a still-live component — reading a field
// returns the expected value (pins that the hook runs BEFORE the C++
// destructor).
TEST(ComponentVtable, OnDestroyReceivesValidComponentPointer) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);
  // marker defaults to 42; the hook adds 1 only when marker reads 42, else
  // adds 1000. A non-1 value would reveal that the hook saw a destructed
  // object.
  world.destroyEntity(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 1);
}

// Calling destroyEntity twice on the same id is safe — the second call is a
// no-op since the entity is already dead — and the hook fires only once.
TEST(ComponentVtable, DestroyEntityDoesNotCallOnDestroyTwice) {
  World world;
  DestroyHookTracked::resetCounts();
  registerWithDestroyHook<DestroyHookTracked>(world, &DestroyHookTracked::onDestroyHook);

  EntityId id = world.createEntity();
  world.addComponent<DestroyHookTracked>(id);

  world.destroyEntity(id);
  world.destroyEntity(id);
  EXPECT_EQ(DestroyHookTracked::destroyCount.load(), 1);
}
