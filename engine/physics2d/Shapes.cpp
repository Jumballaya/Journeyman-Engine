#include "Shapes.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Whether the segment from `from` to `to` passes through the open box
// (-extent, extent): the slab test, with touching an edge not counting.
bool segmentCrossesBox(glm::vec2 from, glm::vec2 to, glm::vec2 extent) {
  float enter = 0.0f, leave = 1.0f;
  for (int axis = 0; axis < 2; ++axis) {
    const float start = from[axis], delta = to[axis] - from[axis];
    if (delta == 0.0f) {
      if (start <= -extent[axis] || start >= extent[axis]) return false;  // outside the slab all along
      continue;
    }
    float t0 = (-extent[axis] - start) / delta, t1 = (extent[axis] - start) / delta;
    if (t0 > t1) std::swap(t0, t1);
    enter = std::max(enter, t0);
    leave = std::min(leave, t1);
    if (enter >= leave) return false;
  }
  return true;
}

float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
  const glm::vec2 ab = b - a;
  const float length2 = glm::dot(ab, ab);
  const float t = length2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
  return glm::length(p - (a + ab * t));
}

// Whether the segment comes nearer than `round` to the box (-half, half): the
// box with rounded corners that a's center can't enter for a to touch b.
bool segmentTouchesRoundedBox(glm::vec2 from, glm::vec2 to, glm::vec2 half, float round) {
  if (segmentCrossesBox(from, to, half + glm::vec2(round, 0.0f)) || segmentCrossesBox(from, to, half + glm::vec2(0.0f, round)))
    return true;
  if (round <= 0.0f) return false;
  for (const glm::vec2 corner : {half, -half, glm::vec2(half.x, -half.y), glm::vec2(-half.x, half.y)})
    if (distanceToSegment(corner, from, to) < round) return true;
  return false;
}

// In b's frame, a's center moved from `from` to `to`: the shapes touched when
// it came within their Minkowski sum (boxes' halves add, circles' radii round it).
bool touchedAlong(const Shape& a, const Shape& b, glm::vec2 from, glm::vec2 to) {
  const glm::vec2 half = (a.isCircle() ? glm::vec2(0.0f) : a.half) + (b.isCircle() ? glm::vec2(0.0f) : b.half);
  return segmentTouchesRoundedBox(from, to, half, a.radius + b.radius);
}

}  // namespace

bool overlaps(const Shape& a, const Shape& b) {
  const glm::vec2 at = a.center - b.center;
  return touchedAlong(a, b, at, at);
}

bool touchedDuring(const Shape& a, glm::vec2 aTravel, const Shape& b, glm::vec2 bTravel) {
  const glm::vec2 now = a.center - b.center;
  return touchedAlong(a, b, now - (aTravel - bTravel), now);
}

std::optional<ShapeHit> raycast(const Shape& shape, glm::vec2 origin, glm::vec2 direction, float maxDistance) {
  const glm::vec2 p = origin - shape.center;
  if (shape.isCircle()) {
    const float c = glm::dot(p, p) - shape.radius * shape.radius;
    if (c < 0.0f) return ShapeHit{0.0f, -direction};
    const float b = glm::dot(p, direction);
    const float disc = b * b - c;
    if (b >= 0.0f || disc <= 0.0f) return std::nullopt;  // heading away, or passing by
    const float t = -b - std::sqrt(disc);
    if (t > maxDistance) return std::nullopt;
    return ShapeHit{t, glm::normalize(p + direction * t)};
  }
  if (std::abs(p.x) < shape.half.x && std::abs(p.y) < shape.half.y) return ShapeHit{0.0f, -direction};
  float enter = 0.0f, leave = maxDistance;
  glm::vec2 normal{0.0f};
  for (int axis = 0; axis < 2; ++axis) {
    const float d = direction[axis];
    if (d == 0.0f) {
      if (std::abs(p[axis]) >= shape.half[axis]) return std::nullopt;  // beside it all along
      continue;
    }
    float t0 = (-shape.half[axis] - p[axis]) / d, t1 = (shape.half[axis] - p[axis]) / d;
    if (t0 > t1) std::swap(t0, t1);
    if (t0 > enter) {
      enter = t0;
      normal = glm::vec2(0.0f);
      normal[axis] = d > 0.0f ? -1.0f : 1.0f;
    }
    leave = std::min(leave, t1);
    if (enter >= leave) return std::nullopt;
  }
  return ShapeHit{enter, normal};
}
