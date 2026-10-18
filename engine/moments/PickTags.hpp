#pragma once

#include <algorithm>
#include <array>

#include <glm/glm.hpp>

#include "../core/app/agent/AgentRun.hpp"

// Where the frozen overlay's tags go, in logical px (y down); a tag is a box
// (x, y, width, height).
namespace pick_tags {

// Where a pick's number goes (its tag's top-left), at the mark and off it:
// on an entity outline's top-left corner, above it (below it at the screen's
// top); inside an area's top-left corner; up-left of a point's ring, or
// down or right of it at the screen's edge (lo-hi: the crosshair's box, the
// point at its middle).
inline glm::vec2 numberAt(AgentRun::Pick::Kind kind, glm::vec2 lo, glm::vec2 hi, glm::vec2 tag, glm::vec2 screen) {
  const glm::vec2 lowest(2.0f), highest = glm::max(lowest, screen - tag - 2.0f);
  glm::vec2 at(lo.x, lo.y - tag.y - 1.0f);
  if (kind == AgentRun::Pick::Kind::Area) {
    at = lo + 2.0f;
  } else if (kind == AgentRun::Pick::Kind::Point) {
    const glm::vec2 mid = (lo + hi) * 0.5f;
    at = mid - 4.0f - tag;
    for (int i = 0; i < 2; ++i) {
      if (at[i] < lowest[i]) at[i] = mid[i] + 4.0f;
    }
  } else if (at.y < lowest.y) {
    at.y = hi.y + 1.0f;
  }
  return glm::clamp(at, lowest, highest);
}

// The shortest line from a tag to a pick's box lo-hi: its ends on the tag's
// edge and the box's (the same point when they touch).
inline std::array<glm::vec2, 2> leader(glm::vec4 tag, glm::vec2 lo, glm::vec2 hi) {
  const glm::vec2 a(tag.x, tag.y), b = a + glm::vec2(tag.z, tag.w);
  std::array<glm::vec2, 2> ends;
  for (int i = 0; i < 2; ++i) {  // per axis: the middle of where they overlap, else each one's side facing the other
    if (b[i] < lo[i]) {
      ends[0][i] = b[i], ends[1][i] = lo[i];
    } else if (a[i] > hi[i]) {
      ends[0][i] = a[i], ends[1][i] = hi[i];
    } else {
      ends[0][i] = ends[1][i] = (std::max(a[i], lo[i]) + std::min(b[i], hi[i])) * 0.5f;
    }
  }
  return ends;
}

}  // namespace pick_tags
