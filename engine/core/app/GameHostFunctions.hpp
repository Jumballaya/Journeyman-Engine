#pragma once

class Engine;
class ScriptManager;

// Registers the engine-level script API (entities, world queries, spawning,
// script params, time, game state, app control) and binds it to `engine`.
// Called once from Engine::registerScriptModule; clearGameHostFunctions on
// shutdown.
void registerGameHostFunctions(Engine& engine, ScriptManager& scripts);
void clearGameHostFunctions();
