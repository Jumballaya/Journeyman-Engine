#include <gtest/gtest.h>

#include <string>

#include "AssetHandle.hpp"
#include "AssetRegistry.hpp"

// insert then get returns the stored value; second insert for the same handle
// overwrites.
TEST(AssetRegistry, InsertGetRoundtrip) {
  AssetRegistry<std::string> reg;
  AssetHandle h{42};

  EXPECT_EQ(reg.get(h), nullptr);
  reg.insert(h, "hello");

  const std::string* got = reg.get(h);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(*got, "hello");
}

// contains reflects insert/erase state.
TEST(AssetRegistry, ContainsReflectsState) {
  AssetRegistry<int> reg;
  AssetHandle h{7};
  EXPECT_FALSE(reg.contains(h));

  reg.insert(h, 123);
  EXPECT_TRUE(reg.contains(h));

  reg.erase(h);
  EXPECT_FALSE(reg.contains(h));
  EXPECT_EQ(reg.get(h), nullptr);
}

// Different handles live independently in the same registry.
TEST(AssetRegistry, DistinctHandlesAreIndependent) {
  AssetRegistry<int> reg;
  AssetHandle ha{1};
  AssetHandle hb{2};
  reg.insert(ha, 100);
  reg.insert(hb, 200);

  const int* a = reg.get(ha);
  const int* b = reg.get(hb);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(*a, 100);
  EXPECT_EQ(*b, 200);
}
