#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "LockFreeQueue.hpp"

// Single producer + single consumer: items come out in the order they went in.
TEST(LockFreeQueue, SPSCPreservesOrder) {
  constexpr int N = 10'000;
  LockFreeQueue<int> q(1024);
  std::vector<int> consumed;
  consumed.reserve(N);

  std::thread producer([&] {
    for (int i = 0; i < N; ++i) {
      int v = i;
      while (!q.try_enqueue(std::move(v)))
        std::this_thread::yield();
    }
  });
  std::thread consumer([&] {
    int out = 0;
    for (int i = 0; i < N; ++i) {
      while (!q.try_dequeue(out))
        std::this_thread::yield();
      consumed.push_back(out);
    }
  });
  producer.join();
  consumer.join();

  ASSERT_EQ(consumed.size(), static_cast<size_t>(N));
  for (int i = 0; i < N; ++i)
    EXPECT_EQ(consumed[i], i);
}

// Multiple producers, one consumer: every produced item is consumed exactly
// once.
TEST(LockFreeQueue, MPSCNoLossNoDup) {
  constexpr int NP = 4;
  constexpr int PER = 2'500;
  constexpr int TOTAL = NP * PER;
  LockFreeQueue<int> q(4096);

  std::vector<std::thread> producers;
  for (int p = 0; p < NP; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < PER; ++i) {
        int v = p * PER + i;
        while (!q.try_enqueue(std::move(v)))
          std::this_thread::yield();
      }
    });
  }

  std::vector<int> consumed;
  consumed.reserve(TOTAL);
  std::thread consumer([&] {
    int out = 0;
    for (int i = 0; i < TOTAL; ++i) {
      while (!q.try_dequeue(out))
        std::this_thread::yield();
      consumed.push_back(out);
    }
  });

  for (auto &t : producers)
    t.join();
  consumer.join();

  std::sort(consumed.begin(), consumed.end());
  ASSERT_EQ(consumed.size(), static_cast<size_t>(TOTAL));
  for (int i = 0; i < TOTAL; ++i)
    EXPECT_EQ(consumed[i], i);
}

// Multiple producers and multiple consumers: every produced item is consumed
// exactly once. Pins the queue's MPMC design guarantee.
TEST(LockFreeQueue, MPMCNoLossNoDup) {
  constexpr int NP = 4;
  constexpr int NC = 4;
  constexpr int PER = 2'500;
  constexpr int TOTAL = NP * PER;
  LockFreeQueue<int> q(4096);

  std::atomic<int> consumedCount{0};
  std::vector<std::vector<int>> perConsumer(NC);

  std::vector<std::thread> producers;
  for (int p = 0; p < NP; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < PER; ++i) {
        int v = p * PER + i;
        while (!q.try_enqueue(std::move(v)))
          std::this_thread::yield();
      }
    });
  }

  std::vector<std::thread> consumers;
  for (int c = 0; c < NC; ++c) {
    consumers.emplace_back([&, c] {
      int out = 0;
      while (consumedCount.load(std::memory_order_relaxed) < TOTAL) {
        if (q.try_dequeue(out)) {
          perConsumer[c].push_back(out);
          consumedCount.fetch_add(1, std::memory_order_relaxed);
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto &t : producers)
    t.join();
  for (auto &t : consumers)
    t.join();

  std::vector<int> all;
  all.reserve(TOTAL);
  for (auto &v : perConsumer)
    all.insert(all.end(), v.begin(), v.end());
  std::sort(all.begin(), all.end());

  ASSERT_EQ(all.size(), static_cast<size_t>(TOTAL));
  for (int i = 0; i < TOTAL; ++i)
    EXPECT_EQ(all[i], i);
}

// Enqueueing into a full queue returns false rather than blocking or
// overwriting.
TEST(LockFreeQueue, EnqueueOnFullReturnsFalse) {
  LockFreeQueue<int> q(4);
  for (int i = 0; i < 4; ++i) {
    int v = i;
    EXPECT_TRUE(q.try_enqueue(std::move(v)));
  }
  int overflow = 99;
  EXPECT_FALSE(q.try_enqueue(std::move(overflow)));
}

// Dequeueing from an empty queue returns false rather than blocking.
TEST(LockFreeQueue, DequeueEmptyReturnsFalse) {
  LockFreeQueue<int> q(4);
  int out = 0;
  EXPECT_FALSE(q.try_dequeue(out));
}

// The smallest valid queue (capacity=1) round-trips and remains reusable after
// draining.
TEST(LockFreeQueue, CapacityOne) {
  LockFreeQueue<int> q(1);
  int a = 7, b = 8;
  EXPECT_TRUE(q.try_enqueue(std::move(a)));
  EXPECT_FALSE(q.try_enqueue(std::move(b)));

  int out = 0;
  EXPECT_TRUE(q.try_dequeue(out));
  EXPECT_EQ(out, 7);
  EXPECT_FALSE(q.try_dequeue(out));

  // Reusable after drain.
  int c = 9;
  EXPECT_TRUE(q.try_enqueue(std::move(c)));
  EXPECT_TRUE(q.try_dequeue(out));
  EXPECT_EQ(out, 9);
}

// After shutdown(), neither enqueue nor dequeue succeed — shutdown is a hard
// stop, not a drain-then-close.
TEST(LockFreeQueue, ShutdownHaltsOperations) {
  LockFreeQueue<int> q(4);
  int v = 1;
  EXPECT_TRUE(q.try_enqueue(std::move(v)));
  q.shutdown();

  int w = 2;
  EXPECT_FALSE(q.try_enqueue(std::move(w)));
  int out = 0;
  EXPECT_FALSE(q.try_dequeue(out));
}

namespace {
struct Counter {
  static inline std::atomic<int> alive{0};
  Counter() { alive.fetch_add(1); }
  Counter(const Counter &) { alive.fetch_add(1); }
  Counter(Counter &&) noexcept { alive.fetch_add(1); }
  ~Counter() { alive.fetch_sub(1); }
  Counter &operator=(const Counter &) = default;
  Counter &operator=(Counter &&) noexcept = default;
};
} // namespace

// Destroying a queue that still contains items runs the destructor on each one.
TEST(LockFreeQueue, DestructorDestroysLiveItems) {
  Counter::alive.store(0);
  {
    LockFreeQueue<Counter> q(8);
    for (int i = 0; i < 4; ++i) {
      Counter c;
      ASSERT_TRUE(q.try_enqueue(std::move(c)));
    }
    // 4 Counters are in the queue (moved-from locals were destructed at end
    // of each loop iteration).
    EXPECT_EQ(Counter::alive.load(), 4);
  }
  EXPECT_EQ(Counter::alive.load(), 0);
}

// A move-only payload (unique_ptr) round-trips without a copy attempt.
TEST(LockFreeQueue, MoveOnlyPayload) {
  LockFreeQueue<std::unique_ptr<int>> q(4);
  auto p = std::make_unique<int>(42);
  EXPECT_TRUE(q.try_enqueue(std::move(p)));
  EXPECT_EQ(p, nullptr);

  std::unique_ptr<int> out;
  EXPECT_TRUE(q.try_dequeue(out));
  ASSERT_NE(out, nullptr);
  EXPECT_EQ(*out, 42);
}

// size_approx() reflects the number of items currently in the queue. This is
// the observable the ThreadPool load balancer relies on.
TEST(LockFreeQueue, SizeApproxTracksEnqueueDequeue) {
  LockFreeQueue<int> q(16);
  EXPECT_EQ(q.size_approx(), 0u);

  for (int i = 0; i < 10; ++i) {
    int v = i;
    ASSERT_TRUE(q.try_enqueue(std::move(v)));
  }
  EXPECT_EQ(q.size_approx(), 10u);

  int out = 0;
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.try_dequeue(out));
  EXPECT_EQ(q.size_approx(), 6u);

  while (q.try_dequeue(out)) {}
  EXPECT_EQ(q.size_approx(), 0u);
}

// Push-then-pop many more items than capacity, forcing the internal indices
// to wrap through every slot repeatedly. Exercises the Vyukov sequence
// arithmetic across cycles, not just the first pass through the buffer.
TEST(LockFreeQueue, WrapsAroundCorrectly) {
  LockFreeQueue<int> q(4);
  int out = 0;
  for (int i = 0; i < 1000; ++i) {
    int v = i;
    ASSERT_TRUE(q.try_enqueue(std::move(v)));
    ASSERT_TRUE(q.try_dequeue(out));
    EXPECT_EQ(out, i);
  }
  EXPECT_EQ(q.size_approx(), 0u);
}

// Each producer's items arrive at the consumer in the order that producer
// enqueued them, even though items from different producers are interleaved.
TEST(LockFreeQueue, MPSCPreservesPerProducerOrder) {
  constexpr int NP = 4;
  constexpr int PER = 1000;
  constexpr int TOTAL = NP * PER;
  LockFreeQueue<int> q(4096);

  std::vector<std::thread> producers;
  for (int p = 0; p < NP; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < PER; ++i) {
        int v = p * PER + i;  // producer id encoded as v / PER
        while (!q.try_enqueue(std::move(v))) std::this_thread::yield();
      }
    });
  }

  std::vector<int> consumed;
  consumed.reserve(TOTAL);
  std::thread consumer([&] {
    int out = 0;
    for (int i = 0; i < TOTAL; ++i) {
      while (!q.try_dequeue(out)) std::this_thread::yield();
      consumed.push_back(out);
    }
  });

  for (auto& t : producers) t.join();
  consumer.join();

  for (int p = 0; p < NP; ++p) {
    int expected = p * PER;
    for (int v : consumed) {
      if (v / PER == p) {
        EXPECT_EQ(v, expected) << "producer " << p << " items out of order";
        ++expected;
      }
    }
    EXPECT_EQ(expected, p * PER + PER);
  }
}
