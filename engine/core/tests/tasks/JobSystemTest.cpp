#include <gtest/gtest.h>

#include <atomic>

#include "JobSystem.hpp"

// A dependency cycle can never run; execute() runs the rest and returns
// instead of spinning forever.
TEST(JobSystem, DependencyCycleReturnsInsteadOfHanging) {
  TaskGraph graph;
  std::atomic<int> runs{0};
  graph.addTask([&] { ++runs; });
  TaskId a = graph.addTask([&] { ++runs; });
  TaskId b = graph.addTask([&] { ++runs; });
  graph.addDependency(a, b);
  graph.addDependency(b, a);

  JobSystem js(2);
  js.execute(graph);
  EXPECT_EQ(runs.load(), 1);
  EXPECT_FALSE(graph.isComplete());
}
