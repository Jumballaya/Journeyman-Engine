#include <gtest/gtest.h>

#include "LockFreeQueue.hpp"

TEST(LockFreeQueue, RoundTripsSingleValue) {
  LockFreeQueue<int> q(4);
  EXPECT_TRUE(q.try_enqueue(42));
  int out = 0;
  EXPECT_TRUE(q.try_dequeue(out));
  EXPECT_EQ(out, 42);
}

TEST(LockFreeQueue, DequeueEmptyReturnsFalse) {
  LockFreeQueue<int> q(4);
  int out = 0;
  EXPECT_FALSE(q.try_dequeue(out));
}
