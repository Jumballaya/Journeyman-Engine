#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "JobSystem.hpp"
#include "TaskGraph.hpp"

using namespace std::chrono_literals;

// A linear dependency chain A → B → C executes tasks in that order.
TEST(TaskGraph, LinearChainOrdering) {
  TaskGraph graph;
  JobSystem js(2);

  std::vector<int> order;
  std::mutex m;

  auto ta = graph.addTask([&] {
    std::lock_guard<std::mutex> lk(m);
    order.push_back(0);
  });
  auto tb = graph.addTask([&] {
    std::lock_guard<std::mutex> lk(m);
    order.push_back(1);
  });
  auto tc = graph.addTask([&] {
    std::lock_guard<std::mutex> lk(m);
    order.push_back(2);
  });
  graph.addDependency(tb, ta);
  graph.addDependency(tc, tb);

  js.execute(graph);

  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 0);
  EXPECT_EQ(order[1], 1);
  EXPECT_EQ(order[2], 2);
}

// In a diamond (A→B, A→C, {B,C}→D), D does not start until both B and C have
// finished.
TEST(TaskGraph, DiamondDependencyOrdering) {
  TaskGraph graph;
  JobSystem js(4);

  std::atomic<int> clock{0};
  std::atomic<int> aTs{-1}, bTs{-1}, cTs{-1}, dTs{-1};

  auto ta = graph.addTask([&] { aTs.store(clock.fetch_add(1)); });
  auto tb = graph.addTask([&] { bTs.store(clock.fetch_add(1)); });
  auto tc = graph.addTask([&] { cTs.store(clock.fetch_add(1)); });
  auto td = graph.addTask([&] { dTs.store(clock.fetch_add(1)); });

  graph.addDependency(tb, ta);
  graph.addDependency(tc, ta);
  graph.addDependency(td, tb);
  graph.addDependency(td, tc);

  js.execute(graph);

  EXPECT_LT(aTs.load(), bTs.load());
  EXPECT_LT(aTs.load(), cTs.load());
  EXPECT_LT(bTs.load(), dTs.load());
  EXPECT_LT(cTs.load(), dTs.load());
}

// Tasks with no dependencies between them actually run concurrently.
TEST(TaskGraph, IndependentTasksRunInParallel) {
  TaskGraph graph;
  JobSystem js(4);

  constexpr int N = 8;
  std::atomic<int> currentConcurrent{0};
  std::atomic<int> maxConcurrent{0};

  for (int i = 0; i < N; ++i) {
    graph.addTask([&] {
      int cur = currentConcurrent.fetch_add(1) + 1;
      int prev = maxConcurrent.load();
      while (cur > prev && !maxConcurrent.compare_exchange_weak(prev, cur)) {
      }
      std::this_thread::sleep_for(20ms);
      currentConcurrent.fetch_sub(1);
    });
  }

  js.execute(graph);
  EXPECT_GT(maxConcurrent.load(), 1);
}

// isComplete() is false while tasks remain and becomes true once execute()
// finishes.
TEST(TaskGraph, IsCompleteTransitions) {
  TaskGraph graph;
  JobSystem js(2);
  graph.addTask([] {});
  graph.addTask([] {});
  EXPECT_FALSE(graph.isComplete());
  js.execute(graph);
  EXPECT_TRUE(graph.isComplete());
}

// Once execution has started (fetchReadyJobs has been called), mutating the
// graph is a programmer error and trips a debug assert.
#ifndef NDEBUG
TEST(TaskGraphDeathTest, AddTaskAfterFreezeAsserts) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  TaskGraph graph;
  graph.addTask([] {});
  (void)graph.fetchReadyJobs(); // freezes
  EXPECT_DEATH({ graph.addTask([] {}); }, "");
}

TEST(TaskGraphDeathTest, AddDependencyAfterFreezeAsserts) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  TaskGraph graph;
  auto a = graph.addTask([] {});
  auto b = graph.addTask([] {});
  (void)graph.fetchReadyJobs();
  EXPECT_DEATH({ graph.addDependency(b, a); }, "");
}
#endif

// An empty graph is already complete and execute() returns without blocking.
TEST(TaskGraph, EmptyGraphCompletesImmediately) {
  TaskGraph graph;
  JobSystem js(2);
  EXPECT_TRUE(graph.isComplete());

  auto start = std::chrono::steady_clock::now();
  js.execute(graph);
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_TRUE(graph.isComplete());
  EXPECT_LT(elapsed, 100ms);
}
