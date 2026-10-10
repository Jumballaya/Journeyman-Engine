#pragma once

#include "../core/ecs/World.hpp"
#include "Renderer2D.hpp"

// Draws what physics sees over the frame, as thin lines: colliders (solid boxes
// magenta, others green, circles cyan) and terrain (solid white, one-way yellow).
void drawPhysicsOverlay(Renderer2D& renderer, World& world);
