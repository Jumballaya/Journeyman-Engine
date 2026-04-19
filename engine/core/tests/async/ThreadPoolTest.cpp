#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "ThreadPool.hpp"

using namespace std::chrono_literals;

// Every submitted job runs exactly once — no drops, no duplicates.
TEST(ThreadPool, AllJobsRunExactlyOnce) {
  constexpr int N = 2'000;
  std::atomic<int> counter{0};
  {
    ThreadPool pool(4);
    for (int i = 0; i < N; ++i) {
      pool.enqueue([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.waitForIdle();
  }
  EXPECT_EQ(counter.load(), N);
}

// waitForIdle() does not return until every outstanding job has finished.
TEST(ThreadPool, WaitForIdleBlocksUntilJobsFinish) {
  ThreadPool pool(2);
  std::atomic<bool> jobDone{false};

  auto start = std::chrono::steady_clock::now();
  pool.enqueue([&] {
    std::this_thread::sleep_for(50ms);
    jobDone.store(true);
  });
  pool.waitForIdle();
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_TRUE(jobDone.load());
  EXPECT_GE(elapsed, 45ms); // allow small clock slack
}

// Jobs actually execute on more than one worker thread.
TEST(ThreadPool, UsesMultipleWorkers) {
  ThreadPool pool(4);
  std::mutex m;
  std::unordered_set<std::thread::id> ids;

  for (int i = 0; i < 200; ++i) {
    pool.enqueue([&] {
      std::this_thread::sleep_for(1ms);
      std::lock_guard<std::mutex> lk(m);
      ids.insert(std::this_thread::get_id());
    });
  }
  pool.waitForIdle();
  EXPECT_GT(ids.size(), 1u);
}

// The pool destructor joins all worker threads without crashing or hanging.
TEST(ThreadPool, DestructorCleanShutdown) {
  std::atomic<int> counter{0};
  {
    ThreadPool pool(4);
    for (int i = 0; i < 100; ++i) {
      pool.enqueue([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.waitForIdle();
  } // dtor joins; must not crash or hang
  EXPECT_EQ(counter.load(), 100);
}

// A job that throws must not leave the pool's activeJobs counter stuck; if it
// does, waitForIdle() hangs forever. CTest TIMEOUT kills the hang and surfaces
// the failure.
TEST(ThreadPool, ExceptionInJobDoesNotLeakActiveJobs) {
  ThreadPool pool(2);
  pool.enqueue([] { throw std::runtime_error("boom"); });
  pool.enqueue([] { /* normal */ });
  pool.waitForIdle();
  SUCCEED();
}
