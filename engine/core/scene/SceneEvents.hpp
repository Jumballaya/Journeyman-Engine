#pragma once

#include "SceneHandle.hpp"
#include <string>

namespace events {
    /**
     * Emitted when scene loading starts.
     */
    struct SceneLoadStarted {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when scene loading completes.
     */
    struct SceneLoadCompleted {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when scene unloading starts.
     */
    struct SceneUnloadStarted {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when scene unloading completes.
     */
    struct SceneUnloaded {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when a scene becomes active.
     */
    struct SceneActivated {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when a scene becomes inactive.
     */
    struct SceneDeactivated {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when a scene transition starts.
     * (Used in Phase 4 for transitions)
     */
    struct SceneTransitionStarted {
        SceneHandle from;
        SceneHandle to;
        std::string transitionType;  // "fade", "slide", etc.
    };
    
    /**
     * Emitted when a scene transition completes.
     * (Used in Phase 4 for transitions)
     */
    struct SceneTransitionCompleted {
        SceneHandle from;
        SceneHandle to;
    };
}
