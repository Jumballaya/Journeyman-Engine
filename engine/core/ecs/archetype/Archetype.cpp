#include "Archetype.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "../component/ComponentId.hpp"
#include "../component/ComponentRegistry.hpp"

Archetype::Archetype(ArchetypeSignature signature,
                     const ComponentRegistry &registry)
    : _signature(signature) {
  for (size_t bit = 0; bit < ArchetypeSignature::kMaxComponents; ++bit) {
    if (!_signature.bits.test(bit))
      continue;
    _bitIndices.push_back(bit);
  }

  _componentInfos.assign(_bitIndices.size(), nullptr);
  _columns.resize(_bitIndices.size());

  registry.forEachRegisteredComponent([&](ComponentId id) {
    const ComponentInfo *info = registry.getInfo(id);
    if (!info)
      return;
    if (!_signature.bits.test(info->bitIndex))
      return;
    auto it = std::find(_bitIndices.begin(), _bitIndices.end(), info->bitIndex);
    if (it == _bitIndices.end())
      return;
    size_t idx = static_cast<size_t>(it - _bitIndices.begin());
    _componentInfos[idx] = info;
  });

  for (size_t i = 0; i < _componentInfos.size(); ++i) {
    assert(_componentInfos[i] != nullptr &&
           "Archetype signature references an unregistered component bit");
  }
}

Archetype::~Archetype() {
  const uint32_t rows = count();
  for (size_t c = 0; c < _componentInfos.size(); ++c) {
    const auto &info = *_componentInfos[c];
    std::byte *base = _columns[c].data();
    for (uint32_t r = 0; r < rows; ++r) {
      info.destruct(base + r * info.size);
    }
  }
}

uint32_t Archetype::allocateRow(EntityId id) {
  const uint32_t row = count();
  for (size_t c = 0; c < _componentInfos.size(); ++c) {
    const auto &info = *_componentInfos[c];
    auto &column = _columns[c];
    column.resize(column.size() + info.size);
    info.defaultConstruct(column.data() + row * info.size);
  }
  _entities.push_back(id);
  return row;
}

std::optional<EntityId> Archetype::destroyRow(uint32_t row) {
  const uint32_t last = count() - 1;
  assert(row <= last && "destroyRow called with out-of-range row");

  if (row == last) {
    for (size_t c = 0; c < _componentInfos.size(); ++c) {
      const auto &info = *_componentInfos[c];
      auto &column = _columns[c];
      info.destruct(column.data() + row * info.size);
      column.resize(column.size() - info.size);
    }
    _entities.pop_back();
    return std::nullopt;
  }

  for (size_t c = 0; c < _componentInfos.size(); ++c) {
    const auto &info = *_componentInfos[c];
    auto &column = _columns[c];
    std::byte *target = column.data() + row * info.size;
    std::byte *source = column.data() + last * info.size;
    info.destruct(target);
    info.moveConstruct(target, source);
    info.destruct(source);
    column.resize(column.size() - info.size);
  }

  const EntityId swapped = _entities[last];
  _entities[row] = swapped;
  _entities.pop_back();
  return swapped;
}

void *Archetype::columnAt(size_t bitIndex, uint32_t row) {
  const size_t col = columnIndexForBit(bitIndex);
  const auto &info = *_componentInfos[col];
  return _columns[col].data() + row * info.size;
}

const void *Archetype::columnAt(size_t bitIndex, uint32_t row) const {
  const size_t col = columnIndexForBit(bitIndex);
  const auto &info = *_componentInfos[col];
  return _columns[col].data() + row * info.size;
}

void Archetype::moveComponentsTo(Archetype &target, uint32_t srcRow,
                                 uint32_t dstRow,
                                 const ArchetypeSignature &sharedSig) {
  for (size_t c = 0; c < _componentInfos.size(); ++c) {
    const size_t bit = _bitIndices[c];
    if (!sharedSig.bits.test(bit))
      continue;
    const auto &info = *_componentInfos[c];
    std::byte *src = _columns[c].data() + srcRow * info.size;
    void *dst = target.columnAt(bit, dstRow);
    info.destruct(dst);
    info.moveConstruct(dst, src);
  }
}

size_t Archetype::columnIndexForBit(size_t bitIndex) const {
  for (size_t i = 0; i < _bitIndices.size(); ++i) {
    if (_bitIndices[i] == bitIndex)
      return i;
  }
  assert(false && "columnIndexForBit: bitIndex not present in archetype");
  return 0;
}
