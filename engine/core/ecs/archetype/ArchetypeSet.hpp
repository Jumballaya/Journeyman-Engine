#pragma once

#include <memory>
#include <unordered_map>

#include "Archetype.hpp"
#include "ArchetypeSignature.hpp"

class ComponentRegistry;

// Owns one Archetype per signature; Archetype addresses are stable.
class ArchetypeSet {
public:
  Archetype &getOrCreate(const ArchetypeSignature &signature, const ComponentRegistry &registry) {
    auto &slot = _archetypes[signature];
    if (!slot) slot = std::make_unique<Archetype>(signature, registry);
    return *slot;
  }

  size_t size() const { return _archetypes.size(); }

  template <typename Fn> void forEach(Fn &&fn) {
    for (auto &[_, archetype] : _archetypes) fn(*archetype);
  }

private:
  std::unordered_map<ArchetypeSignature, std::unique_ptr<Archetype>> _archetypes;
};
