#include <gtest/gtest.h>

#include <algorithm>

#include "ScriptLoops.hpp"

TEST(ScriptLoops, AStoppedLoopIsLetGo) {
  ScriptLoops loops;
  const EntityId owner{0, 0};
  for (SoundInstanceId id = 1; id <= 10000; ++id) {
    loops.started(id, owner);
    loops.stopped(id);
  }
  loops.stopped(99);  // not a loop
  EXPECT_EQ(loops.size(), 0u);
}

// A fade isn't an end: a restart mid-fade still takes the fading loop.
TEST(ScriptLoops, ARestartTakesOnlyItsOwnersLoopsFadingOnesToo) {
  ScriptLoops loops;
  const EntityId a{0, 0}, b{1, 0}, aReused{0, 1};
  loops.started(1, a);  // fading out, still playing
  loops.started(2, b);
  loops.started(3, a);
  loops.started(4, aReused);
  std::vector<SoundInstanceId> taken = loops.take(a);
  std::sort(taken.begin(), taken.end());
  EXPECT_EQ(taken, (std::vector<SoundInstanceId>{1, 3}));
  EXPECT_EQ(loops.size(), 2u);
  EXPECT_TRUE(loops.take(a).empty());
}

TEST(ScriptLoops, KeepsOnlyWhatItIsTold) {
  ScriptLoops loops;
  const EntityId live{0, 0}, gone{1, 0};
  loops.started(1, live);
  loops.started(2, live);  // ended in the mixer (stolen, faded out)
  loops.started(3, gone);
  loops.keepOnly([&](SoundInstanceId id, EntityId owner) { return id != 2 && owner == live; });
  EXPECT_EQ(loops.take(live), std::vector<SoundInstanceId>{1});
  EXPECT_EQ(loops.size(), 0u);
}
