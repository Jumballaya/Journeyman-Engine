#include <gtest/gtest.h>

#include "../TestComponents.hpp"
#include "component/ComponentManager.hpp"

// rawStorage returns nullptr for a component type whose storage has never
// been registered with the manager.
TEST(ComponentManager, RawStorageReturnsNullWhenNotRegistered) {
  ComponentManager mgr;
  EXPECT_EQ(mgr.rawStorage(Position::typeId()), nullptr);
}

// rawStorage returns the registered storage pointer once registerStorage has
// been called for that type.
TEST(ComponentManager, RawStorageReturnsStorageWhenRegistered) {
  ComponentManager mgr;
  mgr.registerStorage<Position>();
  EXPECT_NE(mgr.rawStorage(Position::typeId()), nullptr);
  // A different (unregistered) type still returns null on the same manager.
  EXPECT_EQ(mgr.rawStorage(Velocity::typeId()), nullptr);
}
