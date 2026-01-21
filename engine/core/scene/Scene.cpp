#include "Scene.hpp"
#include "SceneGraph.hpp"
#include <stdexcept>
#include <unordered_map>
#include <algorithm>

Scene::Scene(World& world, const std::string& name)
    : _world(world), _name(name), _state(SceneState::Unloaded) {
    // Root entity will be created in onLoad()
}

Scene::~Scene() {
    if (_state != SceneState::Unloaded) {
        onUnload();
    }
}

void Scene::onLoad() {
    validateStateTransition(_state, SceneState::Loading);
    _state = SceneState::Loading;
    
    // Create invisible root entity
    _root = _world.createEntity();
    // Root has no components, is not tracked in _entities
    
    validateStateTransition(_state, SceneState::Active);
    _state = SceneState::Active;
}

void Scene::onUnload() {
    if (_state == SceneState::Unloaded) {
        return;  // Already unloaded
    }
    
    validateStateTransition(_state, SceneState::Unloading);
    _state = SceneState::Unloading;
    
    // Destroy all entities (this will also destroy children recursively)
    // Create a copy of entities set since we're modifying it
    auto entitiesCopy = _entities;
    for (EntityId id : entitiesCopy) {
        if (_world.isAlive(id)) {
            SceneGraph::destroyRecursive(_world, id);
        }
    }
    
    // Destroy root entity
    if (_world.isAlive(_root)) {
        _world.destroyEntity(_root);
    }
    
    // Clear tracking
    _entities.clear();
    _rootEntities.clear();
    _root = EntityId{0, 0};
    
    validateStateTransition(_state, SceneState::Unloaded);
    _state = SceneState::Unloaded;
}

void Scene::onActivate() {
    if (_state == SceneState::Unloaded) {
        throw std::runtime_error("Scene::onActivate() called on unloaded scene");
    }
    if (_state == SceneState::Paused) {
        validateStateTransition(_state, SceneState::Active);
        _state = SceneState::Active;
    } else if (_state == SceneState::Loading) {
        validateStateTransition(_state, SceneState::Active);
        _state = SceneState::Active;
    }
    // If already Active, no transition needed
}

void Scene::onDeactivate() {
    if (_state == SceneState::Unloaded) {
        return;
    }
    if (_state == SceneState::Active) {
        validateStateTransition(_state, SceneState::Paused);
        _state = SceneState::Paused;
    }
}

void Scene::onPause() {
    if (_state == SceneState::Unloaded) {
        return;
    }
    if (_state == SceneState::Active) {
        validateStateTransition(_state, SceneState::Paused);
        _state = SceneState::Paused;
    }
}

void Scene::onResume() {
    if (_state == SceneState::Unloaded) {
        throw std::runtime_error("Scene::onResume() called on unloaded scene");
    }
    if (_state == SceneState::Paused) {
        validateStateTransition(_state, SceneState::Active);
        _state = SceneState::Active;
    }
}

EntityId Scene::createEntity() {
    return createEntity(_root);
}

EntityId Scene::createEntity(EntityId parent) {
    if (_state == SceneState::Unloaded) {
        throw std::runtime_error("Scene::createEntity() called on unloaded scene");
    }
    
    // Validate parent is in this scene (or is root)
    if (parent != _root) {
        if (!hasEntity(parent)) {
            throw std::runtime_error("Scene::createEntity() parent entity not in this scene");
        }
    }
    
    // Create entity in world
    EntityId entity = _world.createEntity();
    
    // Add to scene tracking
    addEntity(entity);
    
    // Set parent if not root
    if (parent != _root) {
        SceneGraph::setParent(_world, entity, parent);
    } else {
        // Make child of root
        SceneGraph::setParent(_world, entity, _root);
        _rootEntities.insert(entity);
    }
    
    return entity;
}

void Scene::destroyEntity(EntityId id) {
    if (!hasEntity(id)) {
        return;  // Not in this scene, ignore
    }
    
    // Remove from root entities if it's a root entity
    _rootEntities.erase(id);
    
    // Destroy recursively (handles children)
    SceneGraph::destroyRecursive(_world, id);
    
    // Remove from tracking
    removeEntity(id);
}

void Scene::reparent(EntityId entity, EntityId newParent) {
    if (!hasEntity(entity)) {
        throw std::runtime_error("Scene::reparent() entity not in this scene");
    }
    
    // Validate new parent
    if (newParent != _root && !hasEntity(newParent)) {
        throw std::runtime_error("Scene::reparent() new parent not in this scene");
    }
    
    // Get current parent
    EntityId oldParent = SceneGraph::getParent(_world, entity);
    
    // Update root entities tracking
    if (oldParent == _root) {
        _rootEntities.erase(entity);
    }
    if (newParent == _root) {
        _rootEntities.insert(entity);
    }
    
    // Reparent
    SceneGraph::setParent(_world, entity, newParent);
}

void Scene::addEntity(EntityId id) {
    _entities.insert(id);
}

void Scene::removeEntity(EntityId id) {
    _entities.erase(id);
    _rootEntities.erase(id);
}

nlohmann::json Scene::serialize() const {
    nlohmann::json sceneJson;
    sceneJson["name"] = _name;
    
    nlohmann::json entitiesJson = nlohmann::json::array();
    
    // Serialize all entities (basic - full implementation in Phase 5)
    forEachEntity([&](EntityId id) {
        nlohmann::json entityJson;
        // TODO: Serialize entity components (Phase 5)
        entitiesJson.push_back(entityJson);
    });
    
    sceneJson["entities"] = entitiesJson;
    return sceneJson;
}

void Scene::deserialize(const nlohmann::json& data) {
    // Basic implementation - full version in Phase 5
    // This is a placeholder that will be implemented when SceneSerializer is created
    throw std::runtime_error("Scene::deserialize() not yet implemented - use SceneManager::loadScene()");
}

void Scene::validateStateTransition(SceneState from, SceneState to) {
    // Define valid transitions
    static const std::unordered_map<SceneState, std::vector<SceneState>> validTransitions = {
        {SceneState::Unloaded, {SceneState::Loading}},
        {SceneState::Loading, {SceneState::Active, SceneState::Unloaded}},
        {SceneState::Active, {SceneState::Paused, SceneState::Unloading}},
        {SceneState::Paused, {SceneState::Active, SceneState::Unloading}},
        {SceneState::Unloading, {SceneState::Unloaded}}
    };
    
    auto it = validTransitions.find(from);
    if (it == validTransitions.end()) {
        throw std::runtime_error("Invalid source state for transition");
    }
    
    const auto& allowed = it->second;
    if (std::find(allowed.begin(), allowed.end(), to) == allowed.end()) {
        throw std::runtime_error(
            "Invalid state transition from " + stateToString(from) + 
            " to " + stateToString(to)
        );
    }
}

std::string Scene::stateToString(SceneState state) {
    switch (state) {
        case SceneState::Unloaded: return "Unloaded";
        case SceneState::Loading: return "Loading";
        case SceneState::Active: return "Active";
        case SceneState::Paused: return "Paused";
        case SceneState::Unloading: return "Unloading";
        default: return "Unknown";
    }
}
