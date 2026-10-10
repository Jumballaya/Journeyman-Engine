#pragma once

#include "../core/ecs/World.hpp"
#include "Renderer2D.hpp"

// Draws what physics sees over the frame, as thin lines: colliders (solid boxes
// magenta, others green, circles cyan) and terrain (solid white, one-way yellow).
void drawPhysicsOverlay(Renderer2D& renderer, World& world);

// A line from a to b, `width` world units wide, drawn at z.
void drawLine(Renderer2D& renderer, glm::vec2 a, glm::vec2 b, glm::vec4 color, float width, float z);
