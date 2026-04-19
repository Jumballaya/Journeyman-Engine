#include <gtest/gtest.h>

#include <atomic>

#include "Job.hpp"
#include "JobSystem.hpp"

// Submit raw jobs directly without building a TaskGraph. Pins that the
// non-graph API surface works, which matters for future non-graph workloads
// (asset streaming, arena flushes, etc.).
TEST(JobSystem, SubmitAndWaitWithoutTaskGraph) {
  JobSystem js(4);
  std::atomic<int> counter{0};

  Job<> j1;
  j1.set([&] { counter.fetch_add(1, std::memory_order_relaxed); });
  js.submit(std::move(j1));

  Job<> j2;
  j2.set([&] { counter.fetch_add(1, std::memory_order_relaxed); });
  js.submit(std::move(j2));

  js.waitForCompletion();
  EXPECT_EQ(counter.load(), 2);
}
