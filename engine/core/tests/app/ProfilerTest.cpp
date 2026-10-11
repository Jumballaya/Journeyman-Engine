#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "Profiler.hpp"

using Kind = Profiler::Kind;

// Each section's average and worst over the window, costliest first; counts as average and max.
TEST(Profiler, ReportsAveragesAndWorstsCostliestFirst) {
  Profiler p;
  for (int f = 0; f < 4; ++f) {
    p.add(Kind::System, "Physics", 0.002);
    p.add(Kind::System, "Scripts", f == 3 ? 0.010 : 0.002);
    p.add(Kind::Script, "a.ts", 0.001);
    p.add(Kind::Script, "a.ts", 0.001);  // two entities running it: one script's total
    p.add(Kind::Count, "entities", 10.0 + f);
    p.endFrame(0.016);
  }
  const nlohmann::json r = p.report();
  EXPECT_EQ(r["frames"], 4);
  EXPECT_EQ(r["frame"]["avgMs"], 16.0);
  ASSERT_EQ(r["systems"].size(), 2u);
  EXPECT_EQ(r["systems"][0]["name"], "Scripts");
  EXPECT_EQ(r["systems"][0]["avgMs"], 4.0);
  EXPECT_EQ(r["systems"][0]["worstMs"], 10.0);
  EXPECT_EQ(r["scripts"][0]["avgMs"], 2.0);
  EXPECT_EQ(r["counts"]["entities"]["avg"], 11.5);
  EXPECT_EQ(r["counts"]["entities"]["max"], 13.0);
}

// Only the last kWindow frames count, and what stopped running a whole window goes.
TEST(Profiler, KeepsTheLastWindowOnly) {
  Profiler p;
  p.add(Kind::Script, "gone.ts", 1.0);
  p.endFrame(1.0);
  for (size_t f = 0; f < Profiler::kWindow; ++f) {
    p.add(Kind::Script, "kept.ts", 0.001);
    p.endFrame(0.010);
  }
  const nlohmann::json r = p.report();
  EXPECT_EQ(r["frames"], Profiler::kWindow);
  EXPECT_EQ(r["frame"]["worstMs"], 10.0);
  ASSERT_EQ(r["scripts"].size(), 1u);
  EXPECT_EQ(r["scripts"][0]["name"], "kept.ts");
}
