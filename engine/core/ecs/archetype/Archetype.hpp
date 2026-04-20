#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../component/ComponentInfo.hpp"
#include "../entity/EntityId.hpp"
#include "ArchetypeSignature.hpp"

class ComponentRegistry;

class Archetype {
public:
  Archetype(ArchetypeSignature signature, const ComponentRegistry &registry);

  Archetype(const Archetype &) = delete;
  Archetype &operator=(const Archetype &) = delete;
  Archetype(Archetype &&) = delete;
  Archetype &operator=(Archetype &&) = delete;

  ~Archetype();

  const ArchetypeSignature &signature() const { return _signature; }
  const std::vector<size_t> &bitIndices() const { return _bitIndices; }
  uint32_t count() const { return static_cast<uint32_t>(_entities.size()); }

  uint32_t allocateRow(EntityId id);
  std::optional<EntityId> destroyRow(uint32_t row);

  void *columnAt(size_t bitIndex, uint32_t row);
  const void *columnAt(size_t bitIndex, uint32_t row) const;

  EntityId entityAt(uint32_t row) const { return _entities[row]; }

  // Precondition: dstRow in `target` was allocated via allocateRow — its
  // columns hold live objects so destruct() is safe before move-construct.
  void moveComponentsTo(Archetype &target, uint32_t srcRow, uint32_t dstRow,
                        const ArchetypeSignature &sharedSig);

private:
  size_t columnIndexForBit(size_t bitIndex) const;

  ArchetypeSignature _signature;
  std::vector<size_t> _bitIndices;
  std::vector<const ComponentInfo *> _componentInfos;
  std::vector<std::vector<std::byte>> _columns;
  std::vector<EntityId> _entities;
};
