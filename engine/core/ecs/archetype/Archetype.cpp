#include "Archetype.hpp"

#include <algorithm>
#include <cassert>

#include "../component/ComponentRegistry.hpp"

Archetype::Archetype(ArchetypeSignature signature, const ComponentRegistry &registry) : _signature(signature) {
  registry.forEachRegisteredComponent([&](ComponentId id) {
    const ComponentInfo *info = registry.getInfo(id);
    if (_signature.bits.test(info->bitIndex)) _columns.push_back({info, {}});
  });
  assert(_columns.size() == _signature.bits.count() && "Archetype signature references an unregistered component bit");
  std::sort(_columns.begin(), _columns.end(),
            [](const Column &a, const Column &b) { return a.info->bitIndex < b.info->bitIndex; });
}

Archetype::~Archetype() {
  for (Column &c : _columns) {
    for (uint32_t r = 0; r < count(); ++r) c.info->destruct(c.at(r));
  }
}

uint32_t Archetype::allocateRow(EntityId id) {
  const uint32_t row = count();
  for (Column &c : _columns) {
    const size_t needed = c.bytes.size() + c.info->size;
    if (needed > c.bytes.capacity()) {
      // Grow by move-construction: vector<byte> would memcpy components, breaking
      // non-relocatable ones (libc++ unordered_map points into itself).
      std::vector<std::byte> grown;
      grown.reserve(std::max(needed, c.bytes.capacity() * 2));
      grown.resize(c.bytes.size());
      for (uint32_t r = 0; r < row; ++r) {
        c.info->moveConstruct(grown.data() + r * c.info->size, c.at(r));
        c.info->destruct(c.at(r));
      }
      c.bytes.swap(grown);
    }
    c.bytes.resize(needed);  // within capacity: no reallocation
    c.info->defaultConstruct(c.at(row));
  }
  _entities.push_back(id);
  return row;
}

std::optional<EntityId> Archetype::destroyRow(uint32_t row) {
  const uint32_t last = count() - 1;
  assert(row <= last && "destroyRow called with out-of-range row");
  for (Column &c : _columns) {
    c.info->destruct(c.at(row));
    if (row != last) {
      c.info->moveConstruct(c.at(row), c.at(last));
      c.info->destruct(c.at(last));
    }
    c.bytes.resize(c.bytes.size() - c.info->size);
  }
  const EntityId moved = _entities[last];
  _entities[row] = moved;
  _entities.pop_back();
  if (row == last) return std::nullopt;
  return moved;
}

Archetype::Column &Archetype::column(size_t bitIndex) {
  auto it = std::find_if(_columns.begin(), _columns.end(), [&](const Column &c) { return c.info->bitIndex == bitIndex; });
  assert(it != _columns.end() && "bitIndex not present in archetype");
  return *it;
}

void *Archetype::columnAt(size_t bitIndex, uint32_t row) { return column(bitIndex).at(row); }

const void *Archetype::columnAt(size_t bitIndex, uint32_t row) const {
  return const_cast<Archetype *>(this)->columnAt(bitIndex, row);
}

void Archetype::moveComponentsTo(Archetype &target, uint32_t srcRow, uint32_t dstRow,
                                 const ArchetypeSignature &sharedSig) {
  for (Column &c : _columns) {
    if (!sharedSig.bits.test(c.info->bitIndex)) continue;
    void *dst = target.columnAt(c.info->bitIndex, dstRow);
    c.info->destruct(dst);
    c.info->moveConstruct(dst, c.at(srcRow));
  }
}
