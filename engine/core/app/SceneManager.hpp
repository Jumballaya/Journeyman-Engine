#pragma once

#include <string>
#include <unordered_map>

#include "AssetManager.hpp"
#include "Scene.hpp"
#include "SceneLoader.hpp"
#include "World.hpp"

// Loading scenes and transitioning scenes
//
//  Transition anatomy:
//
//      1. Scene state -> Paused
//      2. SceneManager state -> Transitioning
//      3. SceneManager emits scene transition out event
//      4. SceneManager clears previous scene data
//      5. SceneManager loads next scene data
//      6. SceneManager emits scene transition in event
//      7. SceneManager state -> Running
//
//      This means that from a script the user can listen for scene 
//      transition events and do a screen fade out, then in again
//      and when the scene is loaded and the transition animation is done
//      the script can move the scene into a playing state
//
class SceneManager {
    public:
        enum State {
            Transitioning,
            Running,
        };

        SceneManager(World& world, AssetManager& assetManager);
        ~SceneManager() = default;

        State getState() const { return _state; }

        Scene& getCurrentScene() {
            return _currentScene;
        }

    private:
        std::unordered_map<SceneHandle, Scene> _scenes;
        std::unordered_map<SceneHandle, std::string> _sceneNames;
        State _state = State::Running;
        Scene& _currentScene;

        SceneLoader _loader;
        World& _world;
        AssetManager& _assetManager;
};
