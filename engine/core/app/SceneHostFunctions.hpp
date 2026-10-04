#pragma once

#include <wasm3.h>

class Engine;
class SceneManager;

void setSceneHostContext(Engine& engine, SceneManager& sceneManager);
void clearSceneHostContext();

m3ApiRawFunction(jmSceneLoad);
m3ApiRawFunction(jmSceneTransition);
m3ApiRawFunction(jmSceneIsTransitioning);
m3ApiRawFunction(jmSceneTransitionWith);
m3ApiRawFunction(jmSceneCurrent);
