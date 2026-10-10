#include "Shapes.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

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
  assert(a.radius == 0.0f || a.half == glm::vec2(0.0f));  // made by Shape::box or Shape::circle
  assert(b.radius == 0.0f || b.half == glm::vec2(0.0f));
  return segmentTouchesRoundedBox(from, to, a.half + b.half, a.radius + b.radius);  // a circle has no half, a box no radius
}

std::optional<ShapeHit> raycastCircle(glm::vec2 p, glm::vec2 direction, float radius, float maxDistance) {
  // In doubles, and through the ray's nearest approach: far circles don't cancel away.
  const glm::dvec2 from(p), along = glm::normalize(glm::dvec2(direction));
  const double r = radius, b = glm::dot(from, along);
  if (glm::dot(from, from) < r * r) return ShapeHit{0.0f, -direction};
  const glm::dvec2 nearest = from - along * b;
  const double disc = r * r - glm::dot(nearest, nearest);
  if (b >= 0.0 || disc <= 0.0) return std::nullopt;  // heading away, or passing by
  const double t = std::max(-b - std::sqrt(disc), 0.0);  // starting on its surface: not a hair behind
  if (t > maxDistance) return std::nullopt;
  return ShapeHit{static_cast<float>(t), glm::normalize(glm::vec2(from + along * t))};
}

std::optional<ShapeHit> raycastBox(glm::vec2 p, glm::vec2 direction, glm::vec2 half, float maxDistance) {
  if (std::abs(p.x) < half.x && std::abs(p.y) < half.y) return ShapeHit{0.0f, -direction};
  float enter = -INFINITY, leave = INFINITY;
  glm::vec2 normal{0.0f};
  for (int axis = 0; axis < 2; ++axis) {
    const float d = direction[axis];
    if (d == 0.0f) {
      if (std::abs(p[axis]) >= half[axis]) return std::nullopt;  // beside it all along
      continue;
    }
    float t0 = (-half[axis] - p[axis]) / d, t1 = (half[axis] - p[axis]) / d;
    if (t0 > t1) std::swap(t0, t1);
    if (t0 > enter) {  // the face it crosses last is the one it enters by; corners go to x
      enter = t0;
      normal = glm::vec2(0.0f);
      normal[axis] = d > 0.0f ? -1.0f : 1.0f;
    }
    leave = std::min(leave, t1);
  }
  if (enter >= leave || leave <= 0.0f || enter > maxDistance) return std::nullopt;  // grazing, behind, or too far
  return ShapeHit{std::max(enter, 0.0f), normal};
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
  return shape.kind == Shape::Kind::Circle ? raycastCircle(p, direction, shape.radius, maxDistance)
                                           : raycastBox(p, direction, shape.half, maxDistance);
}

namespace {

double cross(glm::dvec2 u, glm::dvec2 v) { return u.x * v.y - u.y * v.x; }
double halfUlp(float v) { return 0.5 * (std::nextafter(std::fabs(v), INFINITY) - std::fabs(v)); }

}  // namespace

std::optional<ShapeHit> raycastSegment(glm::vec2 a, glm::vec2 b, bool oneWay, glm::vec2 origin, glm::vec2 direction,
                                       float maxDistance) {
  // In doubles: big coordinates don't overflow or cancel away.
  const glm::dvec2 along = glm::dvec2(b) - glm::dvec2(a), toA = glm::dvec2(a) - glm::dvec2(origin), d(direction);
  const double denom = cross(d, along);
  const double t = cross(toA, along) / denom, u = cross(toA, d) / denom;
  glm::dvec2 normal = glm::normalize(glm::dvec2(-along.y, along.x));
  // Starting on it (within the origin's rounding to floats, and this arithmetic's) is touching, not
  // crossing: feet on the ground pass.
  const double on = glm::dot(glm::abs(normal), glm::dvec2(halfUlp(origin.x), halfUlp(origin.y))) +
                    4.0 * std::numeric_limits<double>::epsilon() * glm::dot(glm::abs(toA), glm::abs(glm::dvec2(along.y, along.x))) /
                        glm::length(along);
  const double height = t * std::abs(glm::dot(normal, d));
  if (!(height > on && t <= maxDistance && u >= 0.0 && u <= 1.0)) return std::nullopt;  // also parallel, or NaN
  if (oneWay && normal.y < 0.0) normal = -normal;  // its top side
  if (glm::dot(normal, d) > 0.0) {
    if (oneWay) return std::nullopt;  // from below
    normal = -normal;
  }
  if (oneWay && (normal.y <= 0.0 || d.y >= 0.0)) return std::nullopt;  // upright (no top), or not heading down
  return ShapeHit{static_cast<float>(t), glm::vec2(normal)};
}

bool overlapsSegment(const Shape& shape, glm::vec2 a, glm::vec2 b) {
  if (shape.kind == Shape::Kind::Circle) return distanceToSegment(shape.center, a, b) < shape.radius;
  return segmentCrossesBox(a - shape.center, b - shape.center, shape.half);
}
