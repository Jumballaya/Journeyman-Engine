#include <gtest/gtest.h>

#include "World.hpp"
#include "archetype/ArchetypeSet.hpp"
#include "archetype/ArchetypeSignature.hpp"
#include "component/Component.hpp"
#include "component/ComponentRegistry.hpp"

namespace {

struct SetA : Component<SetA> {
  COMPONENT_NAME("SetA");
  int v = 0;
};

struct SetB : Component<SetB> {
  COMPONENT_NAME("SetB");
  int v = 0;
};

template <typename T> void registerNoop(World &world) {
  world.registerComponent<T>();
}

ArchetypeSignature sigOf(const ComponentRegistry &reg,
                         std::initializer_list<ComponentId> ids) {
  ArchetypeSignature sig;
  for (ComponentId id : ids)
    sig.bits.set(reg.getInfo(id)->bitIndex);
  return sig;
}

} // namespace

// The same signature maps to the same Archetype instance across getOrCreate
// calls, so the set acts as a single source of truth for each layout.
TEST(ArchetypeSet, GetOrCreateReturnsSameArchetypeForSameSignature) {
  World world;
  registerNoop<SetA>(world);
  registerNoop<SetB>(world);

  const auto &reg = world.getComponentRegistry();
  ArchetypeSet set;

  ArchetypeSignature sig = sigOf(reg, {SetA::typeId(), SetB::typeId()});
  Archetype &a1 = set.getOrCreate(sig, reg);
  Archetype &a2 = set.getOrCreate(sig, reg);
  EXPECT_EQ(&a1, &a2);
  EXPECT_EQ(set.size(), 1u);
}

// Distinct signatures produce distinct archetype instances.
TEST(ArchetypeSet,
     GetOrCreateProducesDistinctArchetypesForDifferentSignatures) {
  World world;
  registerNoop<SetA>(world);
  registerNoop<SetB>(world);

  const auto &reg = world.getComponentRegistry();
  ArchetypeSet set;

  ArchetypeSignature sigA = sigOf(reg, {SetA::typeId()});
  ArchetypeSignature sigAB = sigOf(reg, {SetA::typeId(), SetB::typeId()});

  Archetype &a = set.getOrCreate(sigA, reg);
  Archetype &ab = set.getOrCreate(sigAB, reg);
  EXPECT_NE(&a, &ab);
  EXPECT_EQ(set.size(), 2u);
}
