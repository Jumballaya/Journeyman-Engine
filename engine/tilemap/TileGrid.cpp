#include "TileGrid.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kGap = 0.01f;  // left between a stopped box and the tile it touches

}  // namespace

TileGrid::TileGrid(std::vector<std::string> rows, std::shared_ptr<const Tileset> tileset, float tileSize,
                   Outside outside)
    : _tileset(std::move(tileset)), _tileSize(tileSize > 0.0f ? tileSize : 16.0f), _outside(outside) {
  setRows(std::move(rows));
}

void TileGrid::setRows(std::vector<std::string> rows) {
  _rows = std::move(rows);
  _height = static_cast<int>(_rows.size());
  _width = 0;
  for (const auto& row : _rows) _width = std::max(_width, static_cast<int>(row.size()));
  for (auto& row : _rows) row.resize(static_cast<size_t>(_width), ' ');
}

char TileGrid::at(int tx, int ty) const {
  if (tx < 0) return _outside.left;
  if (tx >= _width) return _outside.right;
  if (ty < 0) return _outside.bottom;
  if (ty >= _height) return _outside.top;
  return _rows[static_cast<size_t>(_height - 1 - ty)][static_cast<size_t>(tx)];
}

void TileGrid::set(int tx, int ty, char c) {
  if (tx < 0 || tx >= _width || ty < 0 || ty >= _height) return;
  _rows[static_cast<size_t>(_height - 1 - ty)][static_cast<size_t>(tx)] = c;
}

bool TileGrid::solid(int tx, int ty) const {
  const TileDef* d = def(tx, ty);
  return d && d->solid;
}

bool TileGrid::is(int tx, int ty, std::string_view tag) const {
  const TileDef* d = def(tx, ty);
  return d && (tag == "solid" ? d->solid : d->hasTag(tag));
}

char TileGrid::under(int tx, int ty) const {
  const TileDef* d = def(tx, ty);
  if (!d || d->under.empty()) return 0;
  for (size_t i = 0; i + 1 < d->under.size(); ++i) {
    const char c = d->under[i];
    if (at(tx, ty + 1) == c || at(tx + 1, ty) == c || at(tx, ty - 1) == c || at(tx - 1, ty) == c) return c;
  }
  return d->under.back();
}

char TileGrid::terrain(int tx, int ty) const {
  const char beneath = under(tx, ty);
  return beneath ? beneath : at(tx, ty);
}

uint8_t TileGrid::mask(int tx, int ty, char self, const TileDef& def) const {
  uint8_t m = 0;
  if (!def.joinsWith(self, terrain(tx, ty + 1))) m |= edges::N;
  if (!def.joinsWith(self, terrain(tx + 1, ty))) m |= edges::E;
  if (!def.joinsWith(self, terrain(tx, ty - 1))) m |= edges::S;
  if (!def.joinsWith(self, terrain(tx - 1, ty))) m |= edges::W;
  return m;
}

bool TileGrid::overlapsSolid(glm::vec2 center, glm::vec2 half) const {
  // Every tile past an edge is that side's `outside` character, so one ring beyond the grid stands for all of them.
  auto range = [&](float low, float high, int size) {
    auto tile = [&](float at) {
      return static_cast<int>(std::clamp(std::floor(at / _tileSize), -1.0f, static_cast<float>(size)));
    };
    return std::pair(tile(low), tile(high));
  };
  const auto [x0, x1] = range(center.x - half.x, center.x + half.x, _width);
  const auto [y0, y1] = range(center.y - half.y, center.y + half.y, _height);
  for (int ty = y0; ty <= y1; ++ty) {
    for (int tx = x0; tx <= x1; ++tx) {
      if (solid(tx, ty)) return true;
    }
  }
  return false;
}

TileGrid::Move TileGrid::move(glm::vec2 center, glm::vec2 half, glm::vec2 delta, float slide) const {
  Move m{center};
  for (float v : {center.x, center.y, half.x, half.y, delta.x, delta.y}) {
    if (!std::isfinite(v)) return m;  // a script's NaN would otherwise be undefined int casts
  }
  moveAxis(m, half, 0, delta.x, delta.y == 0.0f ? slide : 0.0f);
  moveAxis(m, half, 1, delta.y, delta.x == 0.0f ? slide : 0.0f);
  return m;
}

void TileGrid::moveAxis(Move& m, glm::vec2 half, int axis, float delta, float slide) const {
  if (delta == 0.0f) return;
  const int other = 1 - axis;
  const float sign = delta > 0.0f ? 1.0f : -1.0f;
  // Steps of under half a tile, so fast boxes can't skip a thin wall.
  const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / (_tileSize * 0.45f))));
  const float step = delta / static_cast<float>(steps);
  for (int i = 0; i < steps; ++i) {
    glm::vec2 next = m.position;
    next[axis] += step;
    if (!overlapsSolid(next, half)) {
      m.position = next;
      continue;
    }
    // Flush against the blocking tile row/column, and remember the tile.
    const int line = tileOf(next[axis] + sign * half[axis]);
    m.position[axis] = sign > 0.0f ? static_cast<float>(line) * _tileSize - half[axis] - kGap
                                   : static_cast<float>(line + 1) * _tileSize + half[axis] + kGap;
    m.hit[axis] = static_cast<int>(sign);
    const int from = tileOf(m.position[other] - half[other]), to = tileOf(m.position[other] + half[other]);
    const int middle = tileOf(m.position[other]);
    int best = -1;
    for (int t = from; t <= to; ++t) {
      const bool isSolid = axis == 0 ? solid(line, t) : solid(t, line);
      if (isSolid && (best < 0 || std::abs(t - middle) < std::abs(best - middle))) best = t;
    }
    m.hitTile = axis == 0 ? glm::ivec2(line, best) : glm::ivec2(best, line);

    // Nudge toward the nearest opening that would let the move through.
    for (float off = 1.0f; off <= slide; off += 1.0f) {
      for (float side : {-1.0f, 1.0f}) {
        glm::vec2 probe = next;
        probe[other] += side * off;
        if (overlapsSolid(probe, half)) continue;
        m.position[other] += side * std::min(off, std::fabs(delta));
        return;
      }
    }
    return;
  }
}
