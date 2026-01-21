#include "SceneTransition.hpp"

SceneTransition::SceneTransition(float duration)
    : _duration(duration > 0.0f ? duration : 0.1f),  // Minimum 0.1s
      _elapsed(0.0f),
      _progress(0.0f),
      _phase(TransitionPhase::Out),
      _complete(false) {
}

void SceneTransition::update(float dt) {
    if (_complete) {
        return;  // Already complete
    }
    
    _elapsed += dt;
    
    // Calculate phase duration (half of total for each phase)
    float phaseDuration = _duration / 2.0f;
    
    if (_phase == TransitionPhase::Out) {
        // Out phase: fade out old scene
        if (_elapsed >= phaseDuration) {
            // Out phase complete
            _progress = 1.0f;
            onPhaseComplete(TransitionPhase::Out);
            
            // Switch to In phase
            _phase = TransitionPhase::In;
            _elapsed = 0.0f;  // Reset elapsed for In phase
        } else {
            // Calculate progress in Out phase
            _progress = _elapsed / phaseDuration;
        }
    } else {
        // In phase: fade in new scene
        if (_elapsed >= phaseDuration) {
            // In phase complete - transition done
            _progress = 1.0f;
            onPhaseComplete(TransitionPhase::In);
            _complete = true;
        } else {
            // Calculate progress in In phase
            _progress = _elapsed / phaseDuration;
        }
    }
}
