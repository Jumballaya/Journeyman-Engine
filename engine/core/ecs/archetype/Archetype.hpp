#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../component/ComponentInfo.hpp"
#include "../entity/EntityId.hpp"
#include "ArchetypeSignature.hpp"

class ComponentRegistry;

// The entities sharing one set of components, stored column per component.
class Archetype {
public:
  Archetype(ArchetypeSignature signature, const ComponentRegistry &registry);

  Archetype(const Archetype &) = delete;
  Archetype &operator=(const Archetype &) = delete;

  ~Archetype();

  const ArchetypeSignature &signature() const { return _signature; }
  uint32_t count() const { return static_cast<uint32_t>(_entities.size()); }

  // Appends a row of default-constructed components.
  uint32_t allocateRow(EntityId id);
  // Swaps the last row into `row`; returns the entity that moved, if any.
  std::optional<EntityId> destroyRow(uint32_t row);

  void *columnAt(size_t bitIndex, uint32_t row);
  const void *columnAt(size_t bitIndex, uint32_t row) const;

  EntityId entityAt(uint32_t row) const { return _entities[row]; }

  // Moves the components in `sharedSig` into dstRow, which allocateRow made in
  // `target`. The source row is left for the caller to destroy.
  void moveComponentsTo(Archetype &target, uint32_t srcRow, uint32_t dstRow,
                        const ArchetypeSignature &sharedSig);

private:
  struct Column {
    const ComponentInfo *info;
    std::vector<std::byte> bytes;
    std::byte *at(uint32_t row) { return bytes.data() + row * info->size; }
  };
  Column &column(size_t bitIndex);

  ArchetypeSignature _signature;
  std::vector<Column> _columns;  // ascending bitIndex
  std::vector<EntityId> _entities;
};
