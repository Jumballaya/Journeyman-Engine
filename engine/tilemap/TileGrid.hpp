#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "Tileset.hpp"

// A rectangle of map characters over a tileset: what is where, what blocks, and
// how a box moves through it. Tile (0, 0) is the bottom-left; rows are given top
// first. Positions are relative to the grid's bottom-left corner.
class TileGrid {
 public:
  // What lies beyond each side (a map character).
  struct Outside {
    char left = ' ', right = ' ', top = ' ', bottom = ' ';
  };

  TileGrid() = default;
  TileGrid(std::vector<std::string> rows, std::shared_ptr<const Tileset> tileset, float tileSize, Outside outside);

  int width() const { return _width; }
  int height() const { return _height; }
  float tileSize() const { return _tileSize; }

  char at(int tx, int ty) const;
  // Ignored outside the grid.
  void set(int tx, int ty, char c);
  void setRows(std::vector<std::string> rows);

  bool solid(int tx, int ty) const;
  // "solid", or one of the tile's tags.
  bool is(int tx, int ty, std::string_view tag) const;

  int tileOf(float local) const { return static_cast<int>(std::floor(local / _tileSize)); }

  const TileDef* defFor(char c) const { return _tileset ? _tileset->find(c) : nullptr; }
  const Tileset* tileset() const { return _tileset.get(); }
  const TileDef* def(int tx, int ty) const { return defFor(at(tx, ty)); }
  // The character drawn beneath (tx, ty) (see TileDef::under), or 0.
  char under(int tx, int ty) const;
  // Edge mask for drawing `def` at (tx, ty): sides whose neighbour isn't its terrain.
  uint8_t mask(int tx, int ty, char self, const TileDef& def) const;

  // A box (center, half size) moved by `delta` one axis at a time, stopping
  // flush against solid tiles. With `slide` > 0, a move blocked along one axis
  // nudges sideways (up to `slide` units) toward an opening, so doorways are
  // easy to enter. `hit` is -1/+1 for the side blocked on each axis, and
  // `hitTile` the solid tile met last (nearest the box's center), or (-1, -1).
  struct Move {
    glm::vec2 position;
    glm::ivec2 hit{0, 0};
    glm::ivec2 hitTile{-1, -1};
  };
  Move move(glm::vec2 center, glm::vec2 half, glm::vec2 delta, float slide = 0.0f) const;

 private:
  std::vector<std::string> _rows;  // top row first, each padded to _width
  std::shared_ptr<const Tileset> _tileset;
  float _tileSize = 16.0f;
  Outside _outside;
  int _width = 0, _height = 0;

  // The character that decides terrain: a tile's own, or the one beneath it.
  char terrain(int tx, int ty) const;
  bool overlapsSolid(glm::vec2 center, glm::vec2 half) const;
  void moveAxis(Move& m, glm::vec2 half, int axis, float delta, float slide) const;
};
