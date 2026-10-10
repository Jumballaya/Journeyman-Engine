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

TEST(Shapes, ARayFromABoxsFaceOrInsideHasANormal) {
  const Shape box = Shape::box({0, 0}, {10, 10});
  const auto onFace = raycast(box, {-10, 0}, {1, 0}, 5);  // standing flush, probing in
  ASSERT_TRUE(onFace);
  EXPECT_EQ(onFace->distance, 0);
  EXPECT_EQ(onFace->normal, glm::vec2(-1, 0));
  EXPECT_FALSE(raycast(box, {-10, 0}, {-1, 0}, 5));  // on its face, heading away
  const auto inside = raycast(box, {0, 0}, {0, 1}, 5);
  ASSERT_TRUE(inside);
  EXPECT_EQ(inside->normal, glm::vec2(0, -1));
  const auto corner = raycast(box, {-20, -20}, glm::normalize(glm::vec2(1, 1)), 100);  // a corner goes to x
  ASSERT_TRUE(corner);
  EXPECT_EQ(corner->normal, glm::vec2(-1, 0));
}

TEST(Shapes, MaxDistanceIsInclusiveForBoxesAndCircles) {
  EXPECT_TRUE(raycast(Shape::box({10, 0}, {2, 2}), {0, 0}, {1, 0}, 8));
  EXPECT_TRUE(raycast(Shape::circle({10, 0}, 2), {0, 0}, {1, 0}, 8));
}

TEST(Shapes, AFarCircleIsHitPrecisely) {
  const auto hit = raycast(Shape::circle({10000, 0}, 1), {0, 0}, {1, 0}, 20000);
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->distance, 9999);
  EXPECT_EQ(hit->normal, glm::vec2(-1, 0));
}

TEST(Shapes, ANegativeRadiusIsAPoint) {
  const Shape point = Shape::circle({0, 0}, -5);
  EXPECT_TRUE(overlaps(point, Shape::box({0, 0}, {1, 1})));
  EXPECT_FALSE(overlaps(point, Shape::circle({3, 0}, 2)));
  EXPECT_FALSE(raycast(point, {-10, -10}, glm::normalize(glm::vec2(1, 1)), 100));
}

TEST(Shapes, CirclesSweepAgainstCirclesAndMovingBoxes) {
  // A small fast circle crosses a big one within a frame; moving together, they don't meet.
  EXPECT_TRUE(touchedDuring(Shape::circle({100, 0}, 1), {200, 0}, Shape::circle({0, 0}, 3), {0, 0}));
  EXPECT_FALSE(touchedDuring(Shape::circle({100, 0}, 1), {200, 0}, Shape::circle({0, 50}, 3), {200, 0}));
  // A box sweeping past a still circle: within its half and the radius it hits, beyond it doesn't.
  EXPECT_TRUE(touchedDuring(Shape::box({100, 6.8f}, {5, 5}), {200, 0}, Shape::circle({0, 0}, 2), {0, 0}));
  EXPECT_FALSE(touchedDuring(Shape::box({100, 7.5f}, {5, 5}), {200, 0}, Shape::circle({0, 0}, 2), {0, 0}));
}
