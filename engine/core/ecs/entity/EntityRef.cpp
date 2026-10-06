#include "EntityRef.hpp"

#include "../World.hpp"

EntityRef::EntityRef(EntityId id, World* world)
    : id(id), world(world) {}

void EntityRef::addTag(std::string_view tag) {
  world->addTag(id, tag);
}

void EntityRef::removeTag(std::string_view tag) {
  world->removeTag(id, tag);
}

bool EntityRef::hasTag(std::string_view tag) const {
  return world->hasTag(id, tag);
}

bool EntityRef::alive() const {
  return world->isAlive(id);
}