#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Archetype.hpp"
#include "ArchetypeSignature.hpp"

class ComponentRegistry;

// Owns one Archetype per signature; Archetype addresses are stable.
// forEach visits them in the order they were created, the same on every
// platform (a hash map's order isn't: MSVC's differs from libc++'s), so views,
// and everything a frame does in view order, repeat exactly across machines.
class ArchetypeSet {
public:
  Archetype& getOrCreate(const ArchetypeSignature& signature, const ComponentRegistry& registry) {
    auto& slot = _archetypes[signature];
    if (!slot) {
      slot = std::make_unique<Archetype>(signature, registry);
      _inOrder.push_back(slot.get());
    }
    return *slot;
  }

  size_t size() const { return _archetypes.size(); }

  template <typename Fn> void forEach(Fn&& fn) {
    for (Archetype* archetype : _inOrder) fn(*archetype);
  }

private:
  std::unordered_map<ArchetypeSignature, std::unique_ptr<Archetype>> _archetypes;
  std::vector<Archetype*> _inOrder;  // creation order
};
