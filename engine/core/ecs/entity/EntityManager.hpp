#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "EntityId.hpp"

// Hands out entity ids, recycling destroyed indices under a new generation.
class EntityManager {
public:
  EntityId create() {
    if (_freeIndices.empty()) {
      _generations.push_back(0);
      return EntityId{static_cast<uint32_t>(_generations.size() - 1), 0};
    }
    const uint32_t index = _freeIndices.front();
    _freeIndices.pop();
    return EntityId{index, _generations[index]};
  }

  void destroy(EntityId id) {
    if (!isAlive(id)) return;
    ++_generations[id.index];
    _freeIndices.push(id.index);
  }

  bool isAlive(EntityId id) const { return id.index < _generations.size() && _generations[id.index] == id.generation; }

private:
  std::vector<uint32_t> _generations;
  std::queue<uint32_t> _freeIndices;
};
