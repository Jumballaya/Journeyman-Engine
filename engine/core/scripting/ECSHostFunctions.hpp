#pragma once

#include <wasm3.h>

#include "../app/Application.hpp"

// Entity Management
m3ApiRawFunction(createEntity);
m3ApiRawFunction(destroyEntity);
m3ApiRawFunction(isEntityAlive);
m3ApiRawFunction(cloneEntity);

// Component Operations
m3ApiRawFunction(hasComponent);
m3ApiRawFunction(getComponentHandle);
m3ApiRawFunction(addComponentFromJSON);
m3ApiRawFunction(removeComponent);

// Tag Operations
m3ApiRawFunction(addTag);
m3ApiRawFunction(removeTag);
m3ApiRawFunction(hasTag);
m3ApiRawFunction(clearTags);
m3ApiRawFunction(findEntitiesWithTag);
