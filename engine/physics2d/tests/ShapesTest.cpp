#include <gtest/gtest.h>

#include "Shapes.hpp"

TEST(Shapes, OverlapIsStrictForEveryPairOfKinds) {
  const Shape box = Shape::box({0, 0}, {10, 10});
  EXPECT_TRUE(overlaps(Shape::box({19, 0}, {10, 10}), box));
  EXPECT_FALSE(overlaps(Shape::box({20, 0}, {10, 10}), box));  // touching edges
  EXPECT_TRUE(overlaps(Shape::circle({14, 0}, 5), box));
  EXPECT_FALSE(overlaps(Shape::circle({15, 0}, 5), box));
  // Near a corner a circle is round: inside the bounding square, not touching.
  EXPECT_FALSE(overlaps(Shape::circle({14, 14}, 5), box));
  EXPECT_TRUE(overlaps(Shape::circle({13, 13}, 5), box));
  EXPECT_TRUE(overlaps(Shape::circle({0, 0}, 1), Shape::circle({1.9f, 0}, 1)));
  EXPECT_FALSE(overlaps(Shape::circle({0, 0}, 1), Shape::circle({2, 0}, 1)));
  EXPECT_TRUE(overlaps(Shape::box({5, 5}, {0, 0}), box));  // a point inside
}

TEST(Shapes, AFastCircleCantPassThroughAThinBoxBetweenFrames) {
  const Shape wall = Shape::box({0, 0}, {1, 50});
  const Shape ball = Shape::circle({30, 0}, 2);  // now past the wall
  EXPECT_FALSE(overlaps(ball, wall));
  EXPECT_TRUE(touchedDuring(ball, {60, 0}, wall, {0, 0}));    // came from x = -30
  EXPECT_FALSE(touchedDuring(ball, {10, 0}, wall, {0, 0}));   // came from x = 20: never reached it
  EXPECT_FALSE(touchedDuring(ball, {60, 120}, wall, {0, 0}));  // came from (-30, -120): crossed x 0 beyond its end
  EXPECT_FALSE(touchedDuring(ball, {60, 0}, wall, {60, 0}));  // moved together: 30 apart all along
}

TEST(Shapes, RaysEnterBoxesAndCirclesFacingTheRay) {
  const auto box = raycast(Shape::box({10, 0}, {2, 2}), {0, 0}, {1, 0}, 100);
  ASSERT_TRUE(box);
  EXPECT_FLOAT_EQ(box->distance, 8);
  EXPECT_EQ(box->normal, glm::vec2(-1, 0));
  const auto circle = raycast(Shape::circle({0, -10}, 3), {0, 0}, {0, -1}, 100);
  ASSERT_TRUE(circle);
  EXPECT_FLOAT_EQ(circle->distance, 7);
  EXPECT_EQ(circle->normal, glm::vec2(0, 1));
  EXPECT_FALSE(raycast(Shape::box({10, 0}, {2, 2}), {0, 0}, {1, 0}, 7.9f));  // out of reach
  EXPECT_FALSE(raycast(Shape::box({10, 0}, {2, 2}), {0, 0}, {-1, 0}, 100));  // behind
  EXPECT_FALSE(raycast(Shape::box({10, 2}, {2, 2}), {0, 0}, {1, 0}, 100));   // grazing its edge
  const auto inside = raycast(Shape::circle({0, 0}, 3), {1, 0}, {0, 1}, 100);
  ASSERT_TRUE(inside);
  EXPECT_EQ(inside->distance, 0);
}
