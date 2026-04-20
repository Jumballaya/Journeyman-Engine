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
