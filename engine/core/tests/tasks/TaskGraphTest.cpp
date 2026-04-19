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

// A deep linear chain still executes strictly in order. Catches ordering bugs
// that only surface past shallow depths.
TEST(TaskGraph, LongChainOrdering) {
  constexpr int N = 25;
  TaskGraph graph;
  JobSystem js(4);

  std::vector<int> order;
  std::mutex m;
  std::vector<TaskId> ids;
  ids.reserve(N);
  for (int i = 0; i < N; ++i) {
    ids.push_back(graph.addTask([&, i] {
      std::lock_guard<std::mutex> lk(m);
      order.push_back(i);
    }));
  }
  for (int i = 1; i < N; ++i) graph.addDependency(ids[i], ids[i - 1]);

  js.execute(graph);

  ASSERT_EQ(order.size(), static_cast<size_t>(N));
  for (int i = 0; i < N; ++i) EXPECT_EQ(order[i], i);
}

// One task with several children. Every child waits for the root before
// running.
TEST(TaskGraph, FanOutGreaterThanTwo) {
  TaskGraph graph;
  JobSystem js(4);

  std::atomic<int> clock{0};
  std::atomic<int> rootTs{-1};
  std::array<std::atomic<int>, 4> childTs;
  for (auto& ts : childTs) ts.store(-1);

  auto root = graph.addTask([&] { rootTs.store(clock.fetch_add(1)); });
  for (int i = 0; i < 4; ++i) {
    auto child = graph.addTask([&, i] { childTs[i].store(clock.fetch_add(1)); });
    graph.addDependency(child, root);
  }

  js.execute(graph);

  for (int i = 0; i < 4; ++i) {
    EXPECT_LT(rootTs.load(), childTs[i].load())
        << "child " << i << " ran before the root";
  }
}

// Several parents feed into a single sink. The sink waits for all of them.
TEST(TaskGraph, FanInGreaterThanTwo) {
  TaskGraph graph;
  JobSystem js(4);

  std::atomic<int> clock{0};
  std::array<std::atomic<int>, 4> parentTs;
  for (auto& ts : parentTs) ts.store(-1);
  std::atomic<int> sinkTs{-1};

  std::array<TaskId, 4> parents;
  for (int i = 0; i < 4; ++i) {
    parents[i] = graph.addTask([&, i] { parentTs[i].store(clock.fetch_add(1)); });
  }
  auto sink = graph.addTask([&] { sinkTs.store(clock.fetch_add(1)); });
  for (int i = 0; i < 4; ++i) graph.addDependency(sink, parents[i]);

  js.execute(graph);

  for (int i = 0; i < 4; ++i) {
    EXPECT_LT(parentTs[i].load(), sinkTs.load())
        << "sink ran before parent " << i;
  }
}

// A graph mixing an independent task with a chained pair. All three run; the
// chain's ordering is preserved; the independent task runs somewhere in there.
TEST(TaskGraph, MixedIndependentAndChainedTasks) {
  TaskGraph graph;
  JobSystem js(4);

  std::atomic<int> clock{0};
  std::atomic<int> freeTs{-1};
  std::atomic<int> chainATs{-1};
  std::atomic<int> chainBTs{-1};

  graph.addTask([&] { freeTs.store(clock.fetch_add(1)); });

  auto a = graph.addTask([&] { chainATs.store(clock.fetch_add(1)); });
  auto b = graph.addTask([&] { chainBTs.store(clock.fetch_add(1)); });
  graph.addDependency(b, a);

  js.execute(graph);

  EXPECT_GE(freeTs.load(), 0);
  EXPECT_GE(chainATs.load(), 0);
  EXPECT_LT(chainATs.load(), chainBTs.load());
}

// A TaskGraph is single-use: calling execute() a second time on the same
// graph does not re-run any tasks.
TEST(TaskGraph, ExecuteTwiceIsNoOpOnSecondCall) {
  TaskGraph graph;
  JobSystem js(2);

  std::atomic<int> runs{0};
  graph.addTask([&] { runs.fetch_add(1, std::memory_order_relaxed); });
  graph.addTask([&] { runs.fetch_add(1, std::memory_order_relaxed); });

  js.execute(graph);
  EXPECT_EQ(runs.load(), 2);

  js.execute(graph);
  EXPECT_EQ(runs.load(), 2) << "a spent graph should not re-run tasks";
}
