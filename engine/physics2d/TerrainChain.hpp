#pragma once

#include <vector>

#include <glm/glm.hpp>

// A line of terrain through points, relative to its entity, with the box
// around them. Plain geometry: tile maps build these without the ECS.
class TerrainChain {
 public:
  TerrainChain(std::vector<glm::vec2> points, bool closed, bool oneWay, bool occludes = false)
      : _points(std::move(points)), _closed(closed), _oneWay(oneWay), _occludes(occludes) {
    if (!_points.empty()) _min = _max = _points[0];
    for (const glm::vec2 p : _points) {
      _min = glm::min(_min, p);
      _max = glm::max(_max, p);
    }
  }
  const std::vector<glm::vec2>& points() const { return _points; }
  bool closed() const { return _closed; }
  bool oneWay() const { return _oneWay; }
  bool occludes() const { return _occludes; }  // blocks shadow-casting lights
  glm::vec2 min() const { return _min; }
  glm::vec2 max() const { return _max; }

  // visit(a, b) for each of its segments; a closed chain's last joins its first.
  template <typename Visit>
  void forEachSegment(Visit visit) const {
    const size_t n = _points.size(), count = _closed && n > 2 ? n : (n > 0 ? n - 1 : 0);
    for (size_t i = 0; i < count; ++i) visit(_points[i], _points[(i + 1) % n]);
  }

 private:
  std::vector<glm::vec2> _points;
  bool _closed, _oneWay, _occludes;
  glm::vec2 _min{0.0f}, _max{0.0f};
};
