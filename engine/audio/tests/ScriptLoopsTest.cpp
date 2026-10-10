#include <gtest/gtest.h>

#include <algorithm>

#include "ScriptLoops.hpp"

// Only loops still playing are held: stopping or fading one, or stopping all, lets it go.
TEST(ScriptLoops, HoldsOnlyLoopsStillPlaying) {
  ScriptLoops loops;
  const EntityId owner{0, 0};
  for (SoundInstanceId id = 1; id <= 10000; ++id) {
    loops.started(id, owner);
    loops.ended(id);
  }
  EXPECT_EQ(loops.size(), 0u);

  loops.started(1, owner);
  loops.started(2, owner);
  loops.ended(2);  // faded
  loops.ended(99);  // not a loop
  EXPECT_EQ(loops.size(), 1u);
  loops.clear();
  EXPECT_EQ(loops.size(), 0u);
}

TEST(ScriptLoops, ARestartTakesOnlyItsOwnersLoops) {
  ScriptLoops loops;
  const EntityId a{0, 0}, b{1, 0}, aReused{0, 1};
  loops.started(1, a);
  loops.started(2, b);
  loops.started(3, a);
  loops.started(4, aReused);
  std::vector<SoundInstanceId> taken = loops.take(a);
  std::sort(taken.begin(), taken.end());
  EXPECT_EQ(taken, (std::vector<SoundInstanceId>{1, 3}));
  EXPECT_EQ(loops.size(), 2u);
  EXPECT_TRUE(loops.take(a).empty());
}

// A destroyed owner's loops play on but aren't held.
TEST(ScriptLoops, ForgetsLoopsOfOwnersThatAreGone) {
  ScriptLoops loops;
  const EntityId live{0, 0}, gone{1, 0};
  loops.started(1, live);
  loops.started(2, gone);
  loops.started(3, gone);
  loops.forgetOwnersNot([&](EntityId e) { return e == live; });
  EXPECT_EQ(loops.size(), 1u);
  EXPECT_EQ(loops.take(live), std::vector<SoundInstanceId>{1});
}
