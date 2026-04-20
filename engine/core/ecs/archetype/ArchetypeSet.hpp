#pragma once

#include <memory>
#include <unordered_map>

#include "Archetype.hpp"
#include "ArchetypeSignature.hpp"

class ComponentRegistry;

class ArchetypeSet {
public:
  Archetype &getOrCreate(const ArchetypeSignature &signature,
                         const ComponentRegistry &registry);
  Archetype *find(const ArchetypeSignature &signature);
  const Archetype *find(const ArchetypeSignature &signature) const;

  size_t size() const { return _archetypes.size(); }

  template <typename Fn> void forEach(Fn &&fn) {
    for (auto &[_, archetype] : _archetypes)
      fn(*archetype);
  }

  template <typename Fn> void forEach(Fn &&fn) const {
    for (const auto &[_, archetype] : _archetypes)
      fn(*archetype);
  }

private:
  std::unordered_map<ArchetypeSignature, std::unique_ptr<Archetype>>
      _archetypes;
};
