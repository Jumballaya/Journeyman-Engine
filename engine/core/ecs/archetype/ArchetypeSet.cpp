#include "ArchetypeSet.hpp"

#include "../component/ComponentRegistry.hpp"

Archetype &ArchetypeSet::getOrCreate(const ArchetypeSignature &signature,
                                     const ComponentRegistry &registry) {
  auto it = _archetypes.find(signature);
  if (it != _archetypes.end()) {
    return *it->second;
  }
  auto archetype = std::make_unique<Archetype>(signature, registry);
  auto [inserted, _] = _archetypes.emplace(signature, std::move(archetype));
  return *inserted->second;
}

Archetype *ArchetypeSet::find(const ArchetypeSignature &signature) {
  auto it = _archetypes.find(signature);
  if (it == _archetypes.end())
    return nullptr;
  return it->second.get();
}

const Archetype *ArchetypeSet::find(const ArchetypeSignature &signature) const {
  auto it = _archetypes.find(signature);
  if (it == _archetypes.end())
    return nullptr;
  return it->second.get();
}
