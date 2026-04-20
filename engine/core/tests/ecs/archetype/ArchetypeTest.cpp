#include <gtest/gtest.h>

#include <cstdint>

#include "World.hpp"
#include "archetype/Archetype.hpp"
#include "archetype/ArchetypeSignature.hpp"
#include "component/Component.hpp"
#include "component/ComponentRegistry.hpp"

namespace {

struct ArchPos : Component<ArchPos> {
  COMPONENT_NAME("ArchPos");
  float x = 0.0f;
  float y = 0.0f;
};

struct ArchVel : Component<ArchVel> {
  COMPONENT_NAME("ArchVel");
  float dx = 0.0f;
  float dy = 0.0f;
};

template <typename T> void registerNoop(World &world) {
  world.registerComponent<T, T>(
      [](World &, EntityId, const nlohmann::json &) {},
      [](const World &, EntityId, nlohmann::json &) { return false; },
      [](World &, EntityId, std::span<const std::byte>) { return false; },
      [](const World &, EntityId, std::span<std::byte>, size_t &) {
        return false;
      });
}

ArchetypeSignature signatureOf(const ComponentRegistry &reg,
                               std::initializer_list<ComponentId> ids) {
  ArchetypeSignature sig;
  for (ComponentId id : ids) {
    const auto *info = reg.getInfo(id);
    sig.bits.set(info->bitIndex);
  }
  return sig;
}

EntityId makeEid(uint32_t index) { return EntityId{index, 1}; }

} // namespace

// Rows store component data contiguously and columnAt returns the value that
// was written for a given row/component pair.
TEST(Archetype, AddRowsAndReadBackValues) {
  World world;
  registerNoop<ArchPos>(world);
  registerNoop<ArchVel>(world);

  const auto &reg = world.getComponentRegistry();
  ArchetypeSignature sig =
      signatureOf(reg, {ArchPos::typeId(), ArchVel::typeId()});
  Archetype archetype(sig, reg);

  const size_t posBit = reg.getInfo(ArchPos::typeId())->bitIndex;
  const size_t velBit = reg.getInfo(ArchVel::typeId())->bitIndex;

  uint32_t r0 = archetype.allocateRow(makeEid(1));
  uint32_t r1 = archetype.allocateRow(makeEid(2));
  uint32_t r2 = archetype.allocateRow(makeEid(3));

  EXPECT_EQ(r0, 0u);
  EXPECT_EQ(r1, 1u);
  EXPECT_EQ(r2, 2u);
  EXPECT_EQ(archetype.count(), 3u);

  static_cast<ArchPos *>(archetype.columnAt(posBit, r0))->x = 1.0f;
  static_cast<ArchPos *>(archetype.columnAt(posBit, r1))->x = 2.0f;
  static_cast<ArchPos *>(archetype.columnAt(posBit, r2))->x = 3.0f;

  static_cast<ArchVel *>(archetype.columnAt(velBit, r0))->dx = 10.0f;
  static_cast<ArchVel *>(archetype.columnAt(velBit, r1))->dx = 20.0f;
  static_cast<ArchVel *>(archetype.columnAt(velBit, r2))->dx = 30.0f;

  EXPECT_FLOAT_EQ(static_cast<ArchPos *>(archetype.columnAt(posBit, r1))->x,
                  2.0f);
  EXPECT_FLOAT_EQ(static_cast<ArchVel *>(archetype.columnAt(velBit, r2))->dx,
                  30.0f);
  EXPECT_EQ(archetype.entityAt(r1), makeEid(2));
}

// Destroying a non-last row swaps the last row's data into the freed slot and
// returns the displaced entity so callers can update their records.
TEST(Archetype, DestroyRowSwapsAndPops) {
  World world;
  registerNoop<ArchPos>(world);

  const auto &reg = world.getComponentRegistry();
  ArchetypeSignature sig = signatureOf(reg, {ArchPos::typeId()});
  Archetype archetype(sig, reg);
  const size_t posBit = reg.getInfo(ArchPos::typeId())->bitIndex;

  archetype.allocateRow(makeEid(10));
  archetype.allocateRow(makeEid(11));
  archetype.allocateRow(makeEid(12));

  static_cast<ArchPos *>(archetype.columnAt(posBit, 0))->x = 100.0f;
  static_cast<ArchPos *>(archetype.columnAt(posBit, 1))->x = 200.0f;
  static_cast<ArchPos *>(archetype.columnAt(posBit, 2))->x = 300.0f;

  auto swapped = archetype.destroyRow(1);
  ASSERT_TRUE(swapped.has_value());
  EXPECT_EQ(*swapped, makeEid(12));
  EXPECT_EQ(archetype.count(), 2u);
  EXPECT_FLOAT_EQ(static_cast<ArchPos *>(archetype.columnAt(posBit, 1))->x,
                  300.0f);
  EXPECT_EQ(archetype.entityAt(1), makeEid(12));
}

// Destroying the last row does not swap and returns nullopt.
TEST(Archetype, DestroyLastRowReturnsNullopt) {
  World world;
  registerNoop<ArchPos>(world);

  const auto &reg = world.getComponentRegistry();
  ArchetypeSignature sig = signatureOf(reg, {ArchPos::typeId()});
  Archetype archetype(sig, reg);

  archetype.allocateRow(makeEid(1));
  archetype.allocateRow(makeEid(2));

  auto swapped = archetype.destroyRow(1);
  EXPECT_FALSE(swapped.has_value());
  EXPECT_EQ(archetype.count(), 1u);
  EXPECT_EQ(archetype.entityAt(0), makeEid(1));
}

// moveComponentsTo moves shared-bit columns from source row to destination
// row in the target archetype, leaving the source row untouched at the count
// level (the caller destroys the source row).
TEST(Archetype, MoveComponentsToPreservesValues) {
  World world;
  registerNoop<ArchPos>(world);
  registerNoop<ArchVel>(world);

  const auto &reg = world.getComponentRegistry();
  ArchetypeSignature sigPos = signatureOf(reg, {ArchPos::typeId()});
  ArchetypeSignature sigPosVel =
      signatureOf(reg, {ArchPos::typeId(), ArchVel::typeId()});

  Archetype source(sigPos, reg);
  Archetype target(sigPosVel, reg);

  const size_t posBit = reg.getInfo(ArchPos::typeId())->bitIndex;
  const size_t velBit = reg.getInfo(ArchVel::typeId())->bitIndex;

  uint32_t srcRow = source.allocateRow(makeEid(1));
  static_cast<ArchPos *>(source.columnAt(posBit, srcRow))->x = 7.5f;

  uint32_t dstRow = target.allocateRow(makeEid(1));
  static_cast<ArchVel *>(target.columnAt(velBit, dstRow))->dx = 9.0f;

  source.moveComponentsTo(target, srcRow, dstRow, sigPos);

  EXPECT_FLOAT_EQ(static_cast<ArchPos *>(target.columnAt(posBit, dstRow))->x,
                  7.5f);
  EXPECT_FLOAT_EQ(static_cast<ArchVel *>(target.columnAt(velBit, dstRow))->dx,
                  9.0f);
  EXPECT_EQ(source.count(), 1u);
}
