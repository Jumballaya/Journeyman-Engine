#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ecs/component/FieldSchema.hpp"

#include "Project.hpp"

// Scene entries read the way the engine reads them.

// Tidies numbers for saving: editor math makes floats, so whole ones become
// integers (119.0 → 119) and the rest lose float noise (six decimals).
void wholeNumbersAsIntegers(Json& value);

// Objects merge key by key (recursively); anything else is replaced.
Json mergeDeep(const Json& base, const Json& overrides);

// A prefab file's JSON, re-read when the file changes; null if unreadable.
const Json* prefabJson(const Project& project, const std::string& path);

// The components an entry ends up with: its own, or its prefab's with its
// overrides merged in (overrides may add components).
Json effectiveComponents(const Project& project, const Json& entity);

// Whether `component` (a key of effectiveComponents) comes from the entry's
// prefab, and whether the entry overrides `key` inside it.
bool fromPrefab(const Project& project, const Json& entity, const std::string& component);
bool overrides(const Json& entity, const std::string& component, const std::string& key);

// Where edits to a component's fields go: the entry's own component, or, for
// a prefab instance, its override (created on demand).
Json& editableComponent(Json& entity, const std::string& component);
// A field's value as the engine will see it (own, override or prefab); null if unset.
Json fieldValue(const Project& project, const Json& entity, const std::string& component, const std::string& key);

// The children a prefab instance brings from its prefab, with the instance's
// overrides for them ({"children": {name: {...}}}) applied; empty for others.
Json prefabChildren(const Project& project, const Json& entity);

// A prefab's picture: its sprite's texture, else its animation's first frame; empty if none.
std::string prefabImage(const Project& project, const std::string& path);
// What an instance overrides: component → its overridden keys.
std::vector<std::pair<std::string, std::vector<std::string>>> overridesOf(const Json& entity);

// Component schemas (from the engine's registry), by component name.
void setComponentSchemas(std::map<std::string, ComponentSchema> all);
const std::map<std::string, ComponentSchema>& componentSchemas();
const ComponentSchema* componentSchema(const std::string& name);

// A just-added component: its schema's defaults, plus something to grab where
// empty would show nothing (Ground: one 128px line).
Json newComponent(const std::string& name);

// `at`, or the nearest spot down-right of it (scene Y is up) clear of every `standing` position.
glm::vec2 freeSpot(glm::vec2 at, const std::vector<glm::vec2>& standing);

// Display label for a component type: its schema label, else the name minus "Component".
std::string componentLabel(const std::string& name);
// The icon for a component type.
const char* componentIcon(const std::string& name);
// The icon for an entry: its most telling component.
const char* entityIcon(const Json& components);
